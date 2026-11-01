/* winmm.dll proxy for Five Nights at Freddy's 4 (Steam, Fusion build 288).
 * Forwards every winmm export to the system DLL. The game calls timeGetTime from its main loop,
 * which gives a tick on the game thread for shared-night co-op. See fnaf4coop.ini for setup. */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#include <shellapi.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "input.h"
#include "lobby.h"
#include "menu.h"
#include "overlay.h"
#include "rules.h"
#include "sync.h"

#define EXE_TIMESTAMP 0x58CFD2A9u
#define TICK_MS 50
#define STATE_LOG_MS 1000
#define MAX_SCRIPT 32
#define FRAME_TITLE 1
#define REPO_URL "https://github.com/GalllaxyCode/fnaf4-coop"
#define FRAME_TEAM_LOST 4
#define JOIN_TIMEOUT_MS 6000
#define MAX_ADAPTERS 8
#define PROBES_PER_TICK 16 /* about 320 a second, so a /20 takes 13 s and a /24 under one */

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

static SOCKET sock = INVALID_SOCKET, finder = INVALID_SOCKET, prober = INVALID_SOCKET;
static struct sockaddr_in host_addr;
static struct sockaddr_in peers[SYNC_MAX_PEERS];
static Sync sync_state;
static Menu menu;
static Lobby lobby;
static bool in_room, click_taken;
static uint32_t join_started_ms, last_announce_ms, cfg_last_room;
static int cfg_test_no_broadcast;
static struct { uint32_t address, mask; } adapters[MAX_ADAPTERS];
static int adapter_count;
static HHOOK message_hook_handle;
static bool in_background, test_background;
static char cfg_ini[MAX_PATH], cfg_role[16], cfg_host[128];
static int cfg_port, cfg_test_mode, cfg_test_skip_title, cfg_test_kill;
static uint32_t last_tick;

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

static void socket_close(SOCKET *s) {
    if (*s != INVALID_SOCKET) closesocket(*s);
    *s = INVALID_SOCKET;
}

/* Remembers this machine's network addresses, to show the host's and to reach each network. */
static void find_adapters(void) {
    static char buffer[32768];
    ULONG size = sizeof buffer;
    DWORD result = GetAdaptersInfo((IP_ADAPTER_INFO *)buffer, &size);
    adapter_count = 0;
    if (result != NO_ERROR) {
        logline("adapter list unavailable: %lu", result);
        return;
    }
    for (IP_ADAPTER_INFO *a = (IP_ADAPTER_INFO *)buffer; a && adapter_count < MAX_ADAPTERS; a = a->Next) {
        uint32_t ip = inet_addr(a->IpAddressList.IpAddress.String), mask = inet_addr(a->IpAddressList.IpMask.String);
        if (ip == 0 || ip == INADDR_NONE || (ntohl(ip) >> 24) == 127) continue;
        adapters[adapter_count].address = ip;
        adapters[adapter_count].mask = mask;
        adapter_count++;
    }
}

static const char *best_address(void) {
    static char text[32];
    uint32_t best = 0;
    int best_rank = -1;
    for (int i = 0; i < adapter_count; i++) {
        uint32_t ip = ntohl(adapters[i].address);
        int rank = (ip >> 16) == 0xC0A8 ? 3 : (ip >> 24) == 10 ? 2 : (ip >> 20) == 0xAC1 ? 1 : 0;
        if (rank > best_rank) {
            best_rank = rank;
            best = adapters[i].address;
        }
    }
    struct in_addr a;
    a.s_addr = best;
    snprintf(text, sizeof text, "%s", best ? inet_ntoa(a) : "finding...");
    return text;
}

static bool net_open(bool bind_port) {
    u_long nonblocking = 1;
    BOOL on = TRUE;
    socket_close(&sock);
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        logline("socket failed: %d", WSAGetLastError());
        return false;
    }
    ioctlsocket(sock, FIONBIO, &nonblocking);
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char *)&on, sizeof on);
    if (!bind_port) return true;
    struct sockaddr_in any;
    memset(&any, 0, sizeof any);
    any.sin_family = AF_INET;
    any.sin_port = htons((u_short)cfg_port);
    if (bind(sock, (struct sockaddr *)&any, sizeof any) != 0) {
        logline("bind %d failed: %d", cfg_port, WSAGetLastError());
        socket_close(&sock);
        return false;
    }
    return true;
}

static void room_host(void) {
    memset(peers, 0, sizeof peers);
    if (!net_open(true)) {
        menu_set_status(&menu, "Could not open a room on this computer");
        return;
    }
    sync_init(&sync_state, true, GetTickCount() ^ (GetCurrentProcessId() << 16), menu.name);
    rules_init(true);
    find_adapters();
    in_room = true;
    menu.room = ROOM_HOSTING;
    menu.status[0] = 0;
    menu.player_count = 1;
    strncpy(menu.my_address, best_address(), LOBBY_ADDRESS_LEN - 1);
    logline("hosting on UDP %d at %s", cfg_port, menu.my_address);
}

static void room_join(uint32_t address, int port) {
    if (!net_open(false)) {
        menu_set_status(&menu, "Could not open a connection");
        return;
    }
    memset(&host_addr, 0, sizeof host_addr);
    host_addr.sin_family = AF_INET;
    host_addr.sin_addr.s_addr = address;
    host_addr.sin_port = htons((u_short)port);
    sync_init(&sync_state, false, GetTickCount() ^ (GetCurrentProcessId() << 16), menu.name);
    rules_init(false);
    in_room = true;
    menu.room = ROOM_JOINING;
    menu.status[0] = 0;
    menu.player_count = 1;
    join_started_ms = real_timeGetTime();
    logline("joining %s port %d", inet_ntoa(host_addr.sin_addr), port);
}

static void room_join_address(const char *text) {
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(text, NULL, &hints, &res) != 0 || !res) {
        menu_set_status(&menu, "That address doesn't look right");
        return;
    }
    room_join(((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr, cfg_port);
    freeaddrinfo(res);
}

static void net_send(const Packet *p);

static void send_goodbye(void) {
    Packet p;
    memset(&p, 0, sizeof p);
    p.magic = SYNC_MAGIC;
    p.version = SYNC_VERSION;
    p.is_host = sync_state.is_host;
    p.session = sync_state.session;
    p.seq = ++sync_state.seq;
    p.frame = -1;
    p.closing = 1;
    net_send(&p);
}

static void room_leave(const char *status) {
    send_goodbye();
    socket_close(&sock);
    in_room = false;
    menu.room = ROOM_NONE;
    menu.player_count = 0;
    menu.players[0] = 0;
    rules_setup(MODE_DEFAULT, 0);
    rules_init(false);
    menu_set_status(&menu, status);
    key_force(16, false);
    key_force(17, false);
    logline("left the room: %s", status[0] ? status : "by choice");
}

static void run_action(MenuAction action) {
    const Room *room;
    switch (action) {
    case ACTION_HOST: room_host(); break;
    case ACTION_JOIN_ROOM:
        room = lobby_room(&lobby, menu.join_index);
        if (room) room_join(room->address, room->port);
        break;
    case ACTION_JOIN_ADDRESS: room_join_address(menu.address); break;
    case ACTION_LEAVE: room_leave(""); break;
    case ACTION_GITHUB: logline("opening the repo: %d", (int)(INT_PTR)ShellExecuteA(NULL, "open", REPO_URL, NULL, NULL, SW_SHOWNORMAL)); break;
    default: break;
    }
}

static void fill_announce(Announce *a, int players) {
    memset(a, 0, sizeof *a);
    a->magic = LOBBY_MAGIC;
    a->version = SYNC_VERSION;
    a->players = (uint8_t)players;
    a->port = (uint16_t)cfg_port;
    a->session = sync_state.session;
    strncpy(a->name, menu.name, SYNC_NAME_LEN - 1);
}

static void announce_room(uint32_t now, int players) {
    Announce a;
    struct sockaddr_in to;
    if (cfg_test_no_broadcast || now - last_announce_ms < LOBBY_ANNOUNCE_MS) return;
    last_announce_ms = now;
    fill_announce(&a, players);
    memset(&to, 0, sizeof to);
    to.sin_family = AF_INET;
    to.sin_port = htons(LOBBY_PORT);
    to.sin_addr.s_addr = INADDR_BROADCAST;
    sendto(sock, (const char *)&a, sizeof a, 0, (struct sockaddr *)&to, sizeof to);
    /* The general broadcast can leave through a VPN, so each network also gets its own. */
    for (int i = 0; i < adapter_count; i++) {
        to.sin_addr.s_addr = adapters[i].address | ~adapters[i].mask;
        sendto(sock, (const char *)&a, sizeof a, 0, (struct sockaddr *)&to, sizeof to);
    }
}

static void read_rooms(SOCKET from_socket, uint32_t now) {
    Announce a;
    struct sockaddr_in from;
    for (;;) {
        int len = sizeof from;
        int n = recvfrom(from_socket, (char *)&a, sizeof a, 0, (struct sockaddr *)&from, &len);
        if (n < 0) {
            /* A probe to an address with nobody on it can come back as this. */
            if (WSAGetLastError() == WSAECONNRESET) continue;
            break;
        }
        if (n != (int)sizeof a) continue;
        /* A host hears its own announcement, which is the one reliable source of its Wi-Fi address. */
        if (a.session == sync_state.session) {
            if (in_room && sync_state.is_host) snprintf(menu.my_address, LOBBY_ADDRESS_LEN, "%s", inet_ntoa(from.sin_addr));
            continue;
        }
        if (a.magic == LOBBY_MAGIC && a.version != SYNC_VERSION) {
            static uint32_t logged;
            if (logged != a.session) logline("room of %.15s at %s is on another version of the mod", a.name, inet_ntoa(from.sin_addr));
            logged = a.session;
            continue;
        }
        bool was_empty = lobby_count(&lobby) == 0;
        if (lobby_note(&lobby, &a, from.sin_addr.s_addr, now) && was_empty) logline("found room %s", a.name);
    }
}

static void find_rooms(uint32_t now) {
    if (finder == INVALID_SOCKET) {
        u_long nonblocking = 1;
        BOOL on = TRUE;
        struct sockaddr_in any;
        finder = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (finder == INVALID_SOCKET) return;
        ioctlsocket(finder, FIONBIO, &nonblocking);
        setsockopt(finder, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof on);
        setsockopt(finder, SOL_SOCKET, SO_BROADCAST, (const char *)&on, sizeof on);
        memset(&any, 0, sizeof any);
        any.sin_family = AF_INET;
        any.sin_port = htons(LOBBY_PORT);
        if (bind(finder, (struct sockaddr *)&any, sizeof any) != 0) logline("cannot listen for rooms: %d", WSAGetLastError());
    }
    read_rooms(finder, now);
    if (prober != INVALID_SOCKET) read_rooms(prober, now);
    lobby_expire(&lobby, now);
}

static void probe_send(uint32_t address) {
    Probe probe;
    struct sockaddr_in to;
    memset(&probe, 0, sizeof probe);
    probe.magic = LOBBY_PROBE_MAGIC;
    probe.version = SYNC_VERSION;
    memset(&to, 0, sizeof to);
    to.sin_family = AF_INET;
    to.sin_port = htons((u_short)cfg_port);
    to.sin_addr.s_addr = address;
    sendto(prober, (const char *)&probe, sizeof probe, 0, (struct sockaddr *)&to, sizeof to);
}

/* Many networks drop broadcasts between devices but carry the packets a join needs, so the panel
 * also asks each address directly: rooms it knows every second, and the rest until one answers. */
static void probe_rooms(uint32_t now) {
    static int adapter, step;
    static uint32_t last_ms;
    if (prober == INVALID_SOCKET) {
        /* Its own port, so the answer can't go to another copy listening for broadcasts. */
        u_long nonblocking = 1;
        prober = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (prober == INVALID_SOCKET) return;
        ioctlsocket(prober, FIONBIO, &nonblocking);
    }
    if (now - last_ms >= LOBBY_ANNOUNCE_MS) {
        last_ms = now;
        if (cfg_last_room) probe_send(cfg_last_room);
        for (int i = 0; i < lobby_count(&lobby); i++) probe_send(lobby_room(&lobby, i)->address);
    }
    if (lobby_count(&lobby) > 0 || adapter_count == 0) return;
    for (int i = 0; i < PROBES_PER_TICK; i++) {
        uint32_t next = lobby_sweep_address(ntohl(adapters[adapter].address), ntohl(adapters[adapter].mask), step++);
        if (next) {
            probe_send(htonl(next));
            continue;
        }
        step = 0;
        adapter++;
        if (adapter < adapter_count) continue;
        adapter = 0;
        find_adapters(); /* the player may have moved to another network */
        if (adapter_count == 0) return;
    }
}

static int peer_slot(const struct sockaddr_in *from, uint32_t now) {
    int free_slot = -1;
    for (int i = 0; i < SYNC_MAX_PEERS; i++) {
        if (peers[i].sin_port == from->sin_port && peers[i].sin_addr.s_addr == from->sin_addr.s_addr) return i;
        bool stale = sync_state.peers[i].used && now - sync_state.peers[i].last_ms >= SYNC_PEER_TIMEOUT_MS;
        if ((peers[i].sin_port == 0 || stale) && free_slot < 0) free_slot = i;
    }
    if (free_slot >= 0) {
        peers[free_slot] = *from;
        sync_state.peers[free_slot].used = false;
        logline("player joined from %s", inet_ntoa(from->sin_addr));
    }
    return free_slot;
}

static void net_poll(uint32_t now) {
    Packet p;
    struct sockaddr_in from;
    if (sock == INVALID_SOCKET) return;
    for (;;) {
        int len = sizeof from;
        int n = recvfrom(sock, (char *)&p, sizeof p, 0, (struct sockaddr *)&from, &len);
        if (n < 0) {
            if (WSAGetLastError() == WSAECONNRESET) continue;
            break;
        }
        if (n == (int)sizeof(Probe) && sync_state.is_host && ((const Probe *)&p)->magic == LOBBY_PROBE_MAGIC) {
            Announce a;
            fill_announce(&a, menu.player_count);
            sendto(sock, (const char *)&a, sizeof a, 0, (struct sockaddr *)&from, sizeof from);
            continue;
        }
        if (n != (int)sizeof p) continue;
        sync_recv(&sync_state, sync_state.is_host ? peer_slot(&from, now) : 0, &p, now);
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

typedef struct { uint32_t at; bool on_title; char command[12]; int value; char text[24]; bool done; } ScriptStep;
static ScriptStep script[MAX_SCRIPT];
static int script_count;

/* Test only: `script=ms:command=value;...`, timed from the start of each night. */
static void script_parse(const char *text) {
    char copy[512];
    strncpy(copy, text, sizeof copy - 1);
    copy[sizeof copy - 1] = 0;
    for (char *item = strtok(copy, ";"); item && script_count < MAX_SCRIPT; item = strtok(NULL, ";")) {
        ScriptStep *s = &script[script_count];
        unsigned at;
        s->on_title = *item == 't';
        if (s->on_title) item++;
        char command[12] = "", value[24] = "";
        if (sscanf(item, "%u:%11[a-z0-9]=%23s", &at, command, value) >= 2) {
            s->at = at;
            strcpy(s->command, command);
            strcpy(s->text, value);
            s->value = atoi(value);
            script_count++;
        }
    }
}

static void script_run(uint32_t elapsed, bool on_title) {
    for (int i = 0; i < script_count; i++) {
        ScriptStep *s = &script[i];
        if (s->done || s->on_title != on_title || elapsed < s->at) continue;
        s->done = true;
        logline("script %s=%d", s->command, s->value);
        if (strcmp(s->command, "av0") == 0) av_write(obj(L"follow"), 0, s->value);
        else if (strcmp(s->command, "x") == 0) { int x, y; if (pos_read(obj(L"follow"), &x, &y)) pos_write(obj(L"follow"), s->value, y); }
        else if (strcmp(s->command, "shot") == 0) {
            char path[MAX_PATH], name[32];
            snprintf(name, sizeof name, "shot%d.bmp", s->value);
            path_beside_self(path, sizeof path, name);
            logline("shot %s", overlay_save(path) ? "saved" : "failed");
        }
        else if (strcmp(s->command, "click") == 0) {
            /* A hardware-level click, so it is routed the way a real mouse click is. */
            RECT r;
            HWND w = overlay_game_window();
            if (w && GetClientRect(w, &r)) {
                POINT at = {(s->value / 10000) * r.right / 1024, (s->value % 10000) * r.bottom / 768};
                ClientToScreen(w, &at);
                DWORD ax = (DWORD)(at.x * 65535 / (GetSystemMetrics(SM_CXSCREEN) - 1)), ay = (DWORD)(at.y * 65535 / (GetSystemMetrics(SM_CYSCREEN) - 1));
                mouse_event(MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE, ax, ay, 0, 0);
                mouse_event(MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_ABSOLUTE, ax, ay, 0, 0);
                mouse_event(MOUSEEVENTF_LEFTUP | MOUSEEVENTF_ABSOLUTE, ax, ay, 0, 0);
            }
        }
        else if (strcmp(s->command, "side") == 0) menu.side = s->value;
        else if (strcmp(s->command, "mode") == 0) menu.mode = s->value;
        else if (strcmp(s->command, "ls") == 0) menu.last_standing = s->value != 0;
        else if (strcmp(s->command, "zone") == 0) rules_command(s->value);
        else if (strcmp(s->command, "dump") == 0) {
            int bx = 0, by = 0, cx = 0, cy = 0;
            pos_read(obj(L"Bonnie"), &bx, &by);
            pos_read(obj(L"living room center"), &cx, &cy);
            logline("dump ai=%d,%d,%d,%d bonnie_rel=%d,%d mark=%d danger=%d", cnt_get(L"Bonnie AI", -1), cnt_get(L"Chica AI", -1), cnt_get(L"Foxy AI", -1), cnt_get(L"Freddy AI", -1), bx - cx, by, cx, cnt_get(L"bonnie danger", -1));
        }
        else if (strcmp(s->command, "shadow") == 0) cnt_set(L"shadow", s->value);
        else if (strcmp(s->command, "chica") == 0) { int x, y; if (pos_read(obj(L"right hall far"), &x, &y)) pos_write(obj(L"Chica"), x + 1, y); }
        else if (strcmp(s->command, "map") == 0) rules_map_click(s->value / 10000, s->value % 10000, real_timeGetTime());
        else if (strcmp(s->command, "front") == 0) SetForegroundWindow(overlay_game_window());
        else if (strcmp(s->command, "bg") == 0) test_background = s->value != 0;
        else if (strcmp(s->command, "turn") == 0) cnt_set(L"force turn", 1);
        else if (strcmp(s->command, "cur") == 0) {
            RECT r;
            HWND w = overlay_game_window();
            if (w && GetClientRect(w, &r)) {
                POINT at = {(s->value / 10000) * r.right / 1024, (s->value % 10000) * r.bottom / 768};
                ClientToScreen(w, &at);
                cursor_set(at.x, at.y);
            }
        }
        else if (strcmp(s->command, "zones") == 0) {
            static const wchar_t *const names[] = {L"run back", L"reset", L"run to left", L"run to center", L"run to right"};
            for (int i = 0; i < 5; i++) {
                int x = 0, y = 0, armed = -1;
                BYTE *o = obj(names[i]);
                pos_read(o, &x, &y);
                av_read(o, 0, &armed);
                logline("zone %d at %d,%d size %dx%d av0=%d found=%d", i, x, y, o ? *(int *)(o + 96) : 0, o ? *(int *)(o + 100) : 0, armed, o != NULL);
            }
        }
        else if (strcmp(s->command, "zonelist") == 0) {
            static const wchar_t *const z[] = {L"living room center", L"living room left", L"living room right", L"left hall far", L"left hall near", L"right hall far", L"right hall near", L"kitchen"};
            for (int i = 0; i < 8; i++) {
                int x = 0, y = 0;
                pos_read(obj(z[i]), &x, &y);
                logline("zone %d = %d,%d", i, x, y);
            }
        }
        else if (strcmp(s->command, "rearm") == 0) av_write(obj(L"run back"), 0, 0);
        else if (strcmp(s->command, "move") == 0) {
            RECT r;
            HWND w = overlay_game_window();
            if (w && GetClientRect(w, &r)) {
                POINT at = {(s->value / 10000) * r.right / 1024, (s->value % 10000) * r.bottom / 768};
                ClientToScreen(w, &at);
                mouse_event(MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE, (DWORD)(at.x * 65535 / (GetSystemMetrics(SM_CXSCREEN) - 1)), (DWORD)(at.y * 65535 / (GetSystemMetrics(SM_CYSCREEN) - 1)), 0, 0);
            }
        }
        else if (strcmp(s->command, "text") == 0) {
            HWND w = overlay_game_window();
            for (const char *c = s->text; *c; c++) PostMessageW(w, WM_CHAR, (WPARAM)(unsigned char)*c, 0);
        }
        else if (strcmp(s->command, "kbd") == 0) {
            /* A hardware-level key press, so typing is tested the way a keyboard produces it. */
            keybd_event((BYTE)s->value, 0, 0, 0);
            keybd_event((BYTE)s->value, 0, KEYEVENTF_KEYUP, 0);
        }
        else if (strcmp(s->command, "vk") == 0) {
            HWND w = overlay_game_window();
            PostMessageW(w, WM_KEYDOWN, (WPARAM)s->value, 0);
            PostMessageW(w, WM_CHAR, (WPARAM)s->value, 0);
            PostMessageW(w, WM_KEYUP, (WPARAM)s->value, 0);
        }
        else if (strcmp(s->command, "key") == 0) key_bot(s->value, true);
        else if (strcmp(s->command, "unkey") == 0) key_bot(s->value, false);
        else if (strcmp(s->command, "hour") == 0) cnt_set(L"hour", s->value);
        else if (strcmp(s->command, "kill") == 0) cfg_test_kill = s->value;
        else if (strcmp(s->command, "goto") == 0) game_goto(s->value);
        else if (strcmp(s->command, "next") == 0) game_next_frame();
        else if (strcmp(s->command, "bonnie") == 0) { int x, y; if (pos_read(obj(L"left hall far"), &x, &y)) pos_write(obj(L"Bonnie"), x + 1, y); }
    }
}

static void log_state(uint32_t now, const Local *l, const Output *o) {
    static uint32_t last;
    if (now - last < STATE_LOG_MS) return;
    last = now;
    int av0 = -1, bx = 0, by = 0, cx = 0, cy = 0, fx = 0, fy = 0, bearx = 0, beary = 0;
    if (l->in_level) {
        av_read(obj(L"follow"), 0, &av0);
        pos_read(obj(L"Bonnie"), &bx, &by);
        pos_read(obj(L"Chica"), &cx, &cy);
        pos_read(obj(L"foxy"), &fx, &fy);
        pos_read(obj(L"Fredbear"), &bearx, &beary);
    }
    if (l->frame == FRAME_PLUSHTRAP || l->frame == FRAME_BB) {
        int px = 0, py = 0, view = -1;
        pos_read(obj(L"plushtrap"), &px, &py);
        av_read(obj(L"Active"), 0, &view);
        logline("mini frame=%d light=%d time=%d active=%d trap=%d,%d view=%d others=%03x %s", l->frame, cnt_get(L"light on", -1), cnt_get(L"Counter", -1), cnt_get(L"becoming active", -1), px, py, view, o->others, sync_state.is_host ? "" : rules_debug());
        return;
    }
    logline("state frame=%d hour=%d night=%d av0=%d doors=%d/%d freddy=%d b=%d,%d c=%d,%d f=%d,%d bear=%d,%d ai=%d shadow=%d rules=%d players=%d bat=%d %s", l->frame, cnt_get(L"hour", -1), cnt_get(L"Night", -1), av0,
            cnt_get(L"left door shut", -1), cnt_get(L"right door shut", -1), cnt_get(L"Freddy counter", -1), bx, by, cx, cy, fx, fy, bearx, beary, cnt_get(L"Fredbear AI", -1), cnt_get(L"shadow", -1), evg_count(), o->player_count, o->battery, rules_debug());
}

/* Every move in the room goes in the log, so a report from a real game can be traced. */
static void log_station(const Local *l, const Output *o) {
    static int last_av0 = -1, lines;
    int av0 = -1, armed = -1;
    if (!l->in_level) {
        last_av0 = -1;
        return;
    }
    av_read(obj(L"follow"), 0, &av0);
    if (av0 == last_av0 || lines >= 4000) return;
    av_read(obj(L"run back"), 0, &armed);
    logline("av0 %d -> %d armed=%d input=%03x others=%03x", last_av0, av0, armed == 0, l->input, o->others);
    last_av0 = av0;
    lines++;
}

static void update_room_panel(const Output *o, uint32_t now) {
    if (menu.room == ROOM_JOINING) {
        if (sync_state.have_host) {
            menu.room = ROOM_JOINED;
            logline("joined the room of %s", sync_state.host.name);
            /* The first place to look next time, when broadcasts don't reach this network. */
            cfg_last_room = host_addr.sin_addr.s_addr;
            WritePrivateProfileStringA("coop", "last_room", inet_ntoa(host_addr.sin_addr), cfg_ini);
        } else if (now - join_started_ms >= JOIN_TIMEOUT_MS) {
            room_leave("Could not reach that room");
            return;
        }
    }
    if (menu.room == ROOM_JOINED) {
        strncpy(menu.room_name, sync_state.host.name, SYNC_NAME_LEN - 1);
        if (o->host_lost) {
            room_leave(sync_state.host_closed ? "The host closed the room" : "Lost connection to the host");
            if (game_frame() != FRAME_TITLE) game_goto(FRAME_TITLE);
            return;
        }
    }
    menu.player_count = 1 + o->player_count;
    snprintf(menu.players, sizeof menu.players, "%s", menu.name);
    for (int i = 0; i < o->player_count; i++) {
        size_t used = strlen(menu.players);
        snprintf(menu.players + used, sizeof menu.players - used, ", %s", o->players[i].name);
    }
}

static bool game_input_blocked(void) {
    if (game_frame() != FRAME_TITLE) return false;
    /* New Game would wipe a client's own save, and the host's frame would pull it straight back. */
    return menu.open || menu.room == ROOM_JOINING || menu.room == ROOM_JOINED;
}

/* Saved as it changes, so no way of leaving the name box can lose it. A half-typed empty name is skipped. */
static void save_name(void) {
    static char saved[SYNC_NAME_LEN];
    if (!menu.name[0] || menu.focus == FOCUS_NAME ? !menu.name[0] : strcmp(menu.name, saved) == 0) return;
    if (strcmp(menu.name, saved) == 0) return;
    strcpy(saved, menu.name);
    WritePrivateProfileStringA("coop", "name", menu.name, cfg_ini);
}

static void log_room_changes(const Local *l, const Output *o) {
    static int mode = -1, state = -1, rescue = -1;
    static bool standing, scaring;
    if (l->scaring != scaring) logline(l->scaring ? "scare started" : "scare over");
    scaring = l->scaring;
    if (o->mode != mode || o->last_standing != standing) logline("mode %s, last one standing %s", menu_mode_name(o->mode), o->last_standing ? "on" : "off");
    if (o->my_state != state) logline("my state %d", o->my_state);
    if ((int)o->rescue != rescue) logline(o->rescue ? "rescue started: %s at station %d" : "no rescue running", o->rescue_name, o->rescue_station);
    mode = o->mode;
    standing = o->last_standing;
    state = o->my_state;
    rescue = o->rescue;
}

static void test_tick(uint32_t now, const Local *l, const Output *o, uint32_t level_ms) {
    static uint32_t title_ms, first_title;
    if (cfg_test_skip_title && l->frame == FRAME_TITLE) {
        if (!title_ms) title_ms = now;
        if (title_ms != 0xFFFFFFFFu && now - title_ms > (uint32_t)cfg_test_skip_title) {
            game_next_frame();
            logline("test: leaving the title screen");
            title_ms = 0xFFFFFFFFu;
        }
    }
    if (level_ms) script_run(now - level_ms, false);
    if (l->frame == FRAME_TITLE) {
        if (!first_title) first_title = now;
        script_run(now - first_title, true);
    }
    if (l->in_level && cfg_test_kill) rules_force_kill(cfg_test_kill);
    if (in_room) log_state(now, l, o);
}

static void poll_mouse(void);
static void update_background(void);

static void coop_tick(uint32_t now) {
    static int last_frame = -1;
    static uint32_t level_ms;
    static bool was_level;
    static OverlayScene scene;
    Local l;
    Output o;
    Packet out;

    if (!game_begin()) return;
    memset(&l, 0, sizeof l);
    memset(&o, 0, sizeof o);
    l.frame = game_frame();
    bool host_away = false;
    if (in_room) {
        rules_setup(menu.mode, menu.side);
        rules_frame();
        rules_read(&l);
        l.mode = menu.mode;
        l.last_standing = menu.last_standing;
        host_away = rules_host_resume(&l, now);
        l.mode = menu.mode;
        l.last_standing = menu.last_standing;
    }
    if (l.frame != last_frame) {
        logline("frame %d (%d rules)", l.frame, evg_count());
        if (l.frame == FRAME_LEVEL && !in_room) logline("solo night, co-op rules %s", rules_host_off_is_active() ? "STILL ON" : "off");
        last_frame = l.frame;
    }
    if (l.frame != FRAME_TITLE) menu.open = false;
    if (l.in_level != was_level) {
        logline(l.in_level ? "night %d started" : "left the night", cnt_get(L"Night", -1));
        was_level = l.in_level;
        if (l.in_level) {
            level_ms = now;
            /* A client rejoining the same night must not replay its script. */
            for (int i = 0; i < script_count; i++)
                if (!script[i].on_title && (sync_state.is_host || !in_room)) script[i].done = false;
        }
        cfg_test_kill = 0;
    }

    if (in_room) {
        net_poll(now);
        sync_step(&sync_state, now, &l, &o, &out);
        net_send(&out);
        if (sync_state.is_host) {
            announce_room(now, 1 + o.player_count);
            find_rooms(now);
        }
        if (!sync_state.is_host) {
            menu.mode = o.mode;
            menu.last_standing = o.last_standing;
            if (menu.side >= (menu_side_count(menu.mode) > 0 ? menu_side_count(menu.mode) : 1)) menu.side = 0;
            /* The host starts as EYES, so a client starts as EARS and someone can always hear. */
            static int seen_mode;
            if (menu.mode == MODE_COOP && seen_mode != MODE_COOP) menu.side = SIDE_EARS;
            seen_mode = menu.mode;
        }
        rules_apply(&o, &l, now);
        if (o.end_night && !host_away && game_frame() == FRAME_LEVEL) {
            logline("everyone is out, the night is over");
            game_goto(FRAME_TEAM_LOST);
        }
        if (o.go_to_frame && rules_may_follow(o.frame, now)) {
            logline("following the host to frame %d", o.frame);
            game_goto(o.frame);
        }
        if (o.apply_kill) {
            static int logged;
            if (logged != o.apply_kill) logline("shared scare %d", o.apply_kill);
            logged = o.apply_kill;
        }
        update_room_panel(&o, now);
        log_station(&l, &o);
        log_room_changes(&l, &o);
    } else if (l.frame == FRAME_TITLE && menu.open) {
        find_rooms(now);
        probe_rooms(now);
    }

    save_name();
    update_background();
    if (l.frame == FRAME_TITLE && !in_background) poll_mouse();
    memset(&scene, 0, sizeof scene);
    if (in_room) rules_scene(&o, &l, now, &scene);
    if (l.frame == FRAME_TITLE) menu_draw(&menu, &lobby, now, &scene);
    overlay_show(&scene);
    key_mute(game_input_blocked() || (in_room && rules_map_open()));

    if (cfg_test_mode) test_tick(now, &l, &o, level_ms);
}

/* A click can arrive twice, once as a message and once from polling the button. */
static bool menu_press(int x, int y, const char *source) {
    static uint32_t last_ms;
    static bool last_taken;
    static int logged;
    uint32_t now = real_timeGetTime();
    if (now - last_ms < 200) return last_taken;
    last_ms = now;
    MenuAction action = ACTION_NONE;
    last_taken = menu_click(&menu, &lobby, x, y, &action);
    if (logged++ < 16) logline("click at %d,%d by %s: %s", x, y, source, last_taken ? "menu" : "game");
    if (last_taken) run_action(action);
    return last_taken;
}

/* Two copies on one Mac share a cursor, and every copy would act on it. A copy in the background
 * leaves the mouse alone. This only starts once the game has seen itself in front, so a failed
 * check can never take the mouse away from a game that is being played. */
static void update_background(void) {
    static bool seen_front;
    HWND front = GetForegroundWindow();
    DWORD owner = 0;
    if (front) GetWindowThreadProcessId(front, &owner);
    bool ours = owner == GetCurrentProcessId();
    if (ours) seen_front = true;
    bool now_background = test_background || (seen_front && !ours);
    if (now_background != in_background) logline(now_background ? "in the background, ignoring the mouse" : "in front");
    in_background = now_background;
    cursor_freeze(in_background);
}

/* Returns true when the message belongs to the panel and the game must not see it. */
static bool menu_message(const MSG *m) {
    MenuAction action = ACTION_NONE;
    int x, y;
    if (in_background && m->message >= WM_MOUSEFIRST && m->message <= WM_MOUSELAST) return true;
    if (rules_map_open() && in_room) {
        if (m->message == WM_LBUTTONDOWN) {
            overlay_view_point(m->pt, &x, &y);
            rules_map_click(x, y, real_timeGetTime());
        }
        return (m->message >= WM_MOUSEFIRST && m->message <= WM_MOUSELAST) || m->message == WM_KEYDOWN || m->message == WM_KEYUP || m->message == WM_CHAR;
    }
    if (game_frame() != FRAME_TITLE) return false;
    bool blocked = game_input_blocked();
    switch (m->message) {
    case WM_MOUSEMOVE:
        return blocked;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        overlay_view_point(m->pt, &x, &y);
        if (menu_press(x, y, "message")) {
            click_taken = true;
            return true;
        }
        return blocked;
    case WM_LBUTTONUP:
        if (click_taken || blocked) {
            click_taken = false;
            return true;
        }
        return false;
    case WM_CHAR:
        if (menu.open) {
            menu_char(&menu, (int)m->wParam, &action);
            run_action(action);
            return true;
        }
        return blocked;
    case WM_KEYDOWN:
        if (menu.open && m->wParam == VK_ESCAPE) {
            menu_escape(&menu, &action);
            run_action(action);
            return true;
        }
        /* The game never sees this message, so the character has to be made here. */
        if (menu.open) TranslateMessage(m);
        return blocked;
    case WM_KEYUP:
        return blocked;
    default:
        return false;
    }
}

/* Sees every message for the game's thread, whichever of its windows the message is aimed at. */
static LRESULT CALLBACK message_hook(int code, WPARAM removing, LPARAM lp) {
    if (code == HC_ACTION && removing == PM_REMOVE) {
        MSG *m = (MSG *)lp;
        if (menu_message(m)) m->message = WM_NULL;
    }
    return CallNextHookEx(NULL, code, removing, lp);
}

/* Polling keeps the button alive even if a window takes the click before the queue shows it. */
static void poll_mouse(void) {
    static bool was_down;
    POINT at;
    int x, y;
    bool down = key_real(VK_LBUTTON);
    if (GetCursorPos(&at)) {
        overlay_view_point(at, &x, &y);
        menu_mouse(&menu, x, y);
        if (down && !was_down) menu_press(x, y, "polling");
    }
    was_down = down;
}

static BOOL CALLBACK log_child(HWND h, LPARAM depth) {
    wchar_t cls[48];
    char text[48];
    RECT r;
    GetClassNameW(h, cls, 48);
    WideCharToMultiByte(CP_ACP, 0, cls, -1, text, sizeof text, NULL, NULL);
    GetClientRect(h, &r);
    logline("%s window %s %ldx%ld", (const char *)depth, text, r.right, r.bottom);
    return TRUE;
}

__attribute__((used)) DWORD coop_timeGetTime(void) {
    DWORD now = real_timeGetTime();
    if (!enabled || GetCurrentThreadId() != main_thread) return now;
    if (!started) {
        started = true;
        logline("first tick on the game thread");
        input_install();
        logline("input hook %s", input_hooked() ? "on" : "off");
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        find_adapters();
        logline("this computer: %s", best_address());
        for (int i = 0; i < adapter_count; i++) {
            struct in_addr a, m;
            char at[20];
            a.s_addr = adapters[i].address;
            m.s_addr = adapters[i].mask;
            snprintf(at, sizeof at, "%s", inet_ntoa(a));
            logline("network %s mask %s", at, inet_ntoa(m));
        }
        if (strcmp(cfg_role, "host") == 0) room_host();
        else if (strcmp(cfg_role, "client") == 0 && cfg_host[0]) room_join_address(cfg_host);
    }
    if (!message_hook_handle && overlay_game_window()) {
        message_hook_handle = SetWindowsHookExW(WH_GETMESSAGE, message_hook, NULL, GetCurrentThreadId());
        logline("message hook %s", message_hook_handle ? "on" : "failed");
        log_child(overlay_game_window(), (LPARAM)"main");
        EnumChildWindows(overlay_game_window(), log_child, (LPARAM)"child");
    }
    if (now - last_tick >= TICK_MS) {
        last_tick = now;
        coop_tick(now);
    }
    return now;
}

static void load_config(void) {
    char logp[MAX_PATH], name[SYNC_NAME_LEN], bot[512], last_room[32];
    path_beside_self(cfg_ini, sizeof cfg_ini, "fnaf4coop.ini");
    path_beside_self(logp, sizeof logp, "fnaf4coop.log");
    GetPrivateProfileStringA("coop", "role", "off", cfg_role, sizeof cfg_role, cfg_ini);
    GetPrivateProfileStringA("coop", "host", "", cfg_host, sizeof cfg_host, cfg_ini);
    GetPrivateProfileStringA("coop", "name", "Player", name, sizeof name, cfg_ini);
    GetPrivateProfileStringA("coop", "last_room", "", last_room, sizeof last_room, cfg_ini);
    if (last_room[0] && inet_addr(last_room) != INADDR_NONE) cfg_last_room = inet_addr(last_room);
    cfg_port = (int)GetPrivateProfileIntA("coop", "port", 47114, cfg_ini);
    cfg_test_mode = (int)GetPrivateProfileIntA("test", "test_mode", 0, cfg_ini);
    cfg_test_skip_title = (int)GetPrivateProfileIntA("test", "skip_title_ms", 0, cfg_ini);
    cfg_test_no_broadcast = (int)GetPrivateProfileIntA("test", "no_broadcast", 0, cfg_ini);
    GetPrivateProfileStringA("test", "script", "", bot, sizeof bot, cfg_ini);
    logf_ = fopen(logp, "w");
    logline("fnaf4coop %s role=%s name=%s", MOD_VERSION, cfg_role, name);
    if (!exe_is_supported()) { logline("this exe is not the supported Steam build, co-op is off"); return; }
    menu_init(&menu, name);
    lobby_clear(&lobby);
    if (cfg_test_mode) script_parse(bot);
    enabled = true;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    self = inst;
    main_thread = GetCurrentThreadId();
    exe_base = (BYTE *)GetModuleHandleA(NULL);
    game_init(exe_base);
    DisableThreadLibraryCalls(inst);
    if (!load_real()) return FALSE;
    load_config();
    return TRUE;
}
