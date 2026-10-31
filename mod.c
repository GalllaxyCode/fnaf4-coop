/* winmm.dll proxy for Five Nights at Freddy's 4 (Steam, Fusion build 288).
 * Forwards every winmm export to the system DLL. The game calls timeGetTime from its main loop,
 * which gives a tick on the game thread for linked-nights co-op. See fnaf4coop.ini for setup. */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "sync.h"

/* Offsets from Clickteam's Cncf.h, confirmed against this exe's code. objInfoList is 146 bytes here,
 * not the header's 142: this runtime has the HWABETA oilColList field at the end. */
#define EXE_TIMESTAMP 0x58CFD2A9u
#define RVA_RUNHEADER 0xB39D4u
#define RH_QUIT 48
#define RH_NUMBER_OI 84
#define RH_OI_LIST 88
#define RH_OBJECT_LIST 0x1278
#define OIL_SIZE 146
#define OIL_OBJECT 6
#define OIL_NAME 90
#define OBJ_LIST_STRIDE 8
#define CNT_TYPE 0x202
#define CNT_VALUE 0x20A
#define CV_LONG 0
#define CV_DOUBLE 2
#define DEATH_FREDDY_COUNTER 80
#define LOOPEXIT_NEXTLEVEL 1

#define NAME_COUNT (sizeof names / sizeof names[0])
static const char *const names[] = {
#include "names.inc"
};
FARPROC g_real[sizeof names / sizeof names[0]];
static DWORD(WINAPI *real_timeGetTime)(void);

static DWORD main_thread;
static HMODULE self;
static FILE *logf_;
static bool enabled;
static bool started;
static BYTE *exe_base;

static SOCKET sock = INVALID_SOCKET;
static struct sockaddr_in host_addr;
static struct sockaddr_in peers[SYNC_MAX_PEERS];
static Sync sync_state;
static char cfg_role[16], cfg_host[128];
static int cfg_port, cfg_test_mode, cfg_test_kill_ms, cfg_test_skip_title, cfg_test_hour_ms, cfg_test_hour;
static uint32_t last_tick, start_ms, level_start_ms;

static void logline(const char *fmt, ...) {
    if (!logf_) return;
    va_list ap;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(logf_, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_start(ap, fmt);
    vfprintf(logf_, fmt, ap);
    va_end(ap);
    fputc('\n', logf_);
    fflush(logf_);
}

static void path_beside_self(char *out, size_t cap, const char *name) {
    GetModuleFileNameA(self, out, (DWORD)cap);
    char *slash = strrchr(out, '\\');
    if (slash) slash[1] = 0;
    strncat(out, name, cap - strlen(out) - 1);
}

static bool load_real(void) {
    char path[MAX_PATH];
    GetSystemDirectoryA(path, sizeof path);
    strncat(path, "\\winmm.dll", sizeof path - strlen(path) - 1);
    HMODULE real = LoadLibraryA(path);
    if (!real || real == self) return false;
    for (size_t i = 0; i < NAME_COUNT; i++) g_real[i] = GetProcAddress(real, names[i]);
    real_timeGetTime = (DWORD(WINAPI *)(void))GetProcAddress(real, "timeGetTime");
    return real_timeGetTime != NULL;
}

static bool exe_is_supported(void) {
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)exe_base;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(exe_base + dos->e_lfanew);
    return nt->FileHeader.TimeDateStamp == EXE_TIMESTAMP;
}

static BYTE *read_ptr(BYTE *at) {
    if (IsBadReadPtr(at, sizeof(BYTE *))) return NULL;
    return *(BYTE **)at;
}

/* Finds a named object's first instance through the frame's object-info list. */
static BYTE *find_object(BYTE *rh, const wchar_t *name) {
    int count = *(int *)(rh + RH_NUMBER_OI);
    BYTE *list = read_ptr(rh + RH_OI_LIST), *objs = read_ptr(rh + RH_OBJECT_LIST);
    if (!list || !objs || count <= 0 || count > 4096) return NULL;
    if (IsBadReadPtr(list, (UINT_PTR)count * OIL_SIZE)) return NULL;
    for (int i = 0; i < count; i++) {
        BYTE *oil = list + i * OIL_SIZE;
        if (wcsncmp((const wchar_t *)(oil + OIL_NAME), name, 24) != 0) continue;
        short first = *(short *)(oil + OIL_OBJECT);
        if (first < 0) return NULL;
        return read_ptr(objs + first * OBJ_LIST_STRIDE);
    }
    return NULL;
}

/* True when the current frame defines the object, even with no instance spawned yet. */
static bool frame_has_object(BYTE *rh, const wchar_t *name) {
    int count = *(int *)(rh + RH_NUMBER_OI);
    BYTE *list = read_ptr(rh + RH_OI_LIST);
    if (!list || count <= 0 || count > 4096 || IsBadReadPtr(list, (UINT_PTR)count * OIL_SIZE)) return false;
    for (int i = 0; i < count; i++)
        if (wcsncmp((const wchar_t *)(list + i * OIL_SIZE + OIL_NAME), name, 24) == 0) return true;
    return false;
}

static bool counter_read(BYTE *obj, int *out) {
    if (!obj || IsBadReadPtr(obj + CNT_TYPE, 16)) return false;
    UINT type = *(UINT *)(obj + CNT_TYPE);
    /* This runtime stores counters inverted and applies `not` on every read. */
    if (type == CV_LONG) *out = ~*(int *)(obj + CNT_VALUE);
    else if (type == CV_DOUBLE) *out = ~(int)*(double *)(obj + CNT_VALUE);
    else return false;
    return true;
}

static void counter_write(BYTE *obj, int value) {
    if (*(UINT *)(obj + CNT_TYPE) == CV_DOUBLE) *(double *)(obj + CNT_VALUE) = ~value;
    else *(int *)(obj + CNT_VALUE) = ~value;
}

static void net_start(void) {
    WSADATA wsa;
    u_long nonblocking = 1;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { logline("WSAStartup failed"); return; }
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) { logline("socket failed: %d", WSAGetLastError()); return; }
    ioctlsocket(sock, FIONBIO, &nonblocking);
    if (sync_state.is_host) {
        struct sockaddr_in any;
        memset(&any, 0, sizeof any);
        any.sin_family = AF_INET;
        any.sin_port = htons((u_short)cfg_port);
        if (bind(sock, (struct sockaddr *)&any, sizeof any) != 0) logline("bind %d failed: %d", cfg_port, WSAGetLastError());
        else logline("hosting on UDP %d", cfg_port);
        return;
    }
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(cfg_host, NULL, &hints, &res) != 0 || !res) { logline("cannot resolve %s", cfg_host); return; }
    memcpy(&host_addr, res->ai_addr, sizeof host_addr);
    host_addr.sin_port = htons((u_short)cfg_port);
    freeaddrinfo(res);
    logline("joining %s port %d", inet_ntoa(host_addr.sin_addr), cfg_port);
}

static int peer_slot(const struct sockaddr_in *from) {
    int free_slot = -1;
    for (int i = 0; i < SYNC_MAX_PEERS; i++) {
        if (peers[i].sin_port == from->sin_port && peers[i].sin_addr.s_addr == from->sin_addr.s_addr) return i;
        if (peers[i].sin_port == 0 && free_slot < 0) free_slot = i;
    }
    if (free_slot >= 0) {
        peers[free_slot] = *from;
        logline("player joined from %s", inet_ntoa(from->sin_addr));
    }
    return free_slot;
}

static void net_poll(uint32_t now) {
    Packet p;
    struct sockaddr_in from;
    int len = sizeof from;
    if (sock == INVALID_SOCKET) return;
    for (;;) {
        len = sizeof from;
        int n = recvfrom(sock, (char *)&p, sizeof p, 0, (struct sockaddr *)&from, &len);
        if (n != (int)sizeof p) {
            if (n < 0 && WSAGetLastError() != WSAEWOULDBLOCK && WSAGetLastError() != WSAECONNRESET) break;
            if (n < 0 && WSAGetLastError() == WSAEWOULDBLOCK) break;
            continue;
        }
        sync_recv(&sync_state, sync_state.is_host ? peer_slot(&from) : 0, &p, now);
    }
}

static void net_send(const Packet *p) {
    if (sock == INVALID_SOCKET) return;
    if (!sync_state.is_host) {
        sendto(sock, (const char *)p, sizeof *p, 0, (struct sockaddr *)&host_addr, sizeof host_addr);
        return;
    }
    for (int i = 0; i < SYNC_MAX_PEERS; i++)
        if (peers[i].sin_port) sendto(sock, (const char *)p, sizeof *p, 0, (struct sockaddr *)&peers[i], sizeof peers[i]);
}

/* Logs each frame's object names once, which proves the list offsets on a real run. */
static void log_frame_objects(BYTE *rh) {
    static BYTE *seen;
    BYTE *list = read_ptr(rh + RH_OI_LIST);
    int count = *(int *)(rh + RH_NUMBER_OI);
    if (!list || list == seen || count <= 0 || count > 4096 || IsBadReadPtr(list, (UINT_PTR)count * OIL_SIZE)) return;
    seen = list;
    logline("frame has %d objects", count);
    for (int i = 0; i < count && i < 6; i++) {
        BYTE *oil = list + i * OIL_SIZE;
        char name[25];
        WideCharToMultiByte(CP_ACP, 0, (const wchar_t *)(oil + OIL_NAME), 24, name, sizeof name - 1, NULL, NULL);
        name[24] = 0;
        logline("  object %d first=%d %s", i, *(short *)(oil + OIL_OBJECT), name);
    }
}

static void coop_tick(uint32_t now) {
    static bool was_level, was_mismatch, forcing;
    static int last_hour = -1, last_result = -1;
    World w = {false, 0, 0, 0, false};
    Writes wr;
    Packet out;
    BYTE *rh = read_ptr(exe_base + RVA_RUNHEADER);
    BYTE *gameover = NULL, *hour = NULL, *night = NULL, *freddy = NULL;

    if (rh && !IsBadReadPtr(rh, RH_OBJECT_LIST + 4)) {
        log_frame_objects(rh);
        gameover = find_object(rh, L"gameover");
        hour = find_object(rh, L"hour");
        night = find_object(rh, L"Night");
        freddy = find_object(rh, L"Freddy counter");
        w.won_screen = frame_has_object(rh, L"num 1"); /* only the `night win` frame has it */
    }
    if (gameover && hour && night && freddy && counter_read(gameover, &w.gameover) && counter_read(hour, &w.hour) &&
        counter_read(night, &w.night))
        w.in_level = true;

    /* Test only: the frames run title, what night, level, so "next frame" reaches a night unattended. */
    if (cfg_test_mode && cfg_test_skip_title && rh && !w.in_level && find_object(rh, L"selection")) {
        static uint32_t title_ms;
        if (!title_ms) title_ms = now;
        if (now - title_ms > (uint32_t)cfg_test_skip_title) {
            *(short *)(rh + RH_QUIT) = LOOPEXIT_NEXTLEVEL;
            title_ms = 0;
            logline("test: leaving the title screen");
        }
    }

    if (w.in_level != was_level) {
        logline(w.in_level ? "night %d started" : "left the night", w.night);
        was_level = w.in_level;
        level_start_ms = now;
        forcing = false;
        last_hour = -1;
    }
    if (w.in_level && w.hour != last_hour) { logline("hour %d", w.hour); last_hour = w.hour; }

    net_poll(now);
    sync_step(&sync_state, now, &w, &wr, &out);
    net_send(&out);

    if (w.in_level) {
        if (wr.set_hour) counter_write(hour, wr.hour);
        if (wr.force_death) {
            counter_write(freddy, DEATH_FREDDY_COUNTER);
            if (!forcing) logline("another player died, starting the kill");
            forcing = true;
        }
        static bool test_hour_done;
        if (cfg_test_mode && cfg_test_hour_ms > 0 && !test_hour_done && now - level_start_ms > (uint32_t)cfg_test_hour_ms) {
            counter_write(hour, cfg_test_hour);
            test_hour_done = true;
            logline("test: set hour to %d", cfg_test_hour);
        }
        if (cfg_test_mode && cfg_test_kill_ms > 0 && !w.gameover && now - level_start_ms > (uint32_t)cfg_test_kill_ms)
            counter_write(freddy, DEATH_FREDDY_COUNTER);
    }
    if (sync_state.is_host && sync_state.result != last_result) {
        logline("result %d", sync_state.result);
        last_result = sync_state.result;
    }
    if (sync_state.night_mismatch != was_mismatch) {
        logline(sync_state.night_mismatch ? "WARNING: you are not on the host's night, the clock is not linked" : "night matches host");
        was_mismatch = sync_state.night_mismatch;
    }
}

__attribute__((used)) DWORD coop_timeGetTime(void) {
    DWORD now = real_timeGetTime();
    if (!enabled || GetCurrentThreadId() != main_thread) return now;
    if (!started) {
        started = true;
        start_ms = now;
        logline("first tick on the game thread");
        net_start();
    }
    if (now - last_tick >= 50) {
        last_tick = now;
        coop_tick(now);
    }
    return now;
}

static void load_config(void) {
    char ini[MAX_PATH], logp[MAX_PATH];
    path_beside_self(ini, sizeof ini, "fnaf4coop.ini");
    path_beside_self(logp, sizeof logp, "fnaf4coop.log");
    GetPrivateProfileStringA("coop", "role", "off", cfg_role, sizeof cfg_role, ini);
    GetPrivateProfileStringA("coop", "host", "", cfg_host, sizeof cfg_host, ini);
    cfg_port = (int)GetPrivateProfileIntA("coop", "port", 47114, ini);
    cfg_test_mode = (int)GetPrivateProfileIntA("test", "test_mode", 0, ini);
    cfg_test_kill_ms = (int)GetPrivateProfileIntA("test", "kill_after_ms", 0, ini);
    cfg_test_skip_title = (int)GetPrivateProfileIntA("test", "skip_title_ms", 0, ini);
    cfg_test_hour_ms = (int)GetPrivateProfileIntA("test", "hour_after_ms", 0, ini);
    cfg_test_hour = (int)GetPrivateProfileIntA("test", "hour_value", 0, ini);
    logf_ = fopen(logp, "w");
    bool is_host = strcmp(cfg_role, "host") == 0, is_client = strcmp(cfg_role, "client") == 0;
    logline("fnaf4coop 1 role=%s", cfg_role);
    if (!is_host && !is_client) return;
    if (is_client && !cfg_host[0]) { logline("role=client needs host="); return; }
    if (!exe_is_supported()) { logline("this exe is not the supported Steam build, co-op is off"); return; }
    sync_init(&sync_state, is_host, GetTickCount() ^ (GetCurrentProcessId() << 16));
    enabled = true;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    self = inst;
    main_thread = GetCurrentThreadId();
    exe_base = (BYTE *)GetModuleHandleA(NULL);
    DisableThreadLibraryCalls(inst);
    if (!load_real()) return FALSE;
    load_config();
    return TRUE;
}
