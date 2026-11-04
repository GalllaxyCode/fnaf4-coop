/* Steam through the flat C API, loaded at run time with no SDK headers, following
 * playbook/networking-steam-spacewar.md. Offsets are for the 32-bit steam_api.dll. */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "steam.h"
#include "sync.h"

#define APP_ID "480"
#define LOBBY_KEY "fnaf4coop" /* marks our lobbies among everyone's Spacewar lobbies */
#define LOBBY_FRIENDS_ONLY 1
#define LOBBY_MAX_MEMBERS SYNC_MAX_PLAYERS
/* Unreliable 0 | NoNagle 1 | AutoRestartBrokenSession 32: a session the other side refused or let
 * time out starts again on the next send instead of failing for good. */
#define SEND_FLAGS (1 | 32)
#define RESULT_OK 1
#define FRIEND_FLAG_IMMEDIATE 4
#define IDENTITY_SIZE 160 /* 136 in the SDK; opaque, only touched through its own calls */
#define PENDING_MAX 8
#define PENDING_MS 15000 /* how long a session from a not-yet-member waits for its lobby entry */
#define ASKED_MAX 16

enum {
    CB_LOBBY_JOIN_REQUESTED = 333,
    CB_RICH_PRESENCE_JOIN_REQUESTED = 337,
    CB_LOBBY_ENTER = 504,
    CB_LOBBY_CHAT_UPDATE = 506,
    CB_LOBBY_CREATED = 513,
    CB_CALL_COMPLETED = 703,
    CB_SESSION_REQUEST = 1251,
    CB_SESSION_FAILED = 1252
};

typedef enum { PHASE_NONE, PHASE_CREATING, PHASE_JOINING, PHASE_IN } Phase;

static SteamLog say;
static bool ready, mock;
static uint64_t me, lobby, owner, join_request;
static Phase phase;
static char failure[96], room_name[STEAM_NAME_LEN], off_reason[48] = "STEAM OFF";

static void set_off(const char *text) { snprintf(off_reason, sizeof off_reason, "%s", text); }

static void set_failure(const char *text) {
    snprintf(failure, sizeof failure, "%s", text);
    say("steam: %s", text);
}

static uint64_t lobby_from_text(const char *text) {
    const char *at = text ? strstr(text, "+connect_lobby") : NULL;
    return at ? _strtoui64(at + 14, NULL, 10) : 0;
}

/* ---------------------------------------------------------------- real Steam */

typedef struct {
    int32_t user;
    int32_t callback;
    uint8_t *param;
    int32_t size;
} CallbackMsg;

static HMODULE api;
static void *user, *matchmaking, *friends, *messages;
static int32_t pipe_handle;
static uint64_t create_call, join_call;
static int sender_offset; /* 12 in the real 32-bit DLL, 16 by the headers' packing: whichever works */
static BYTE identity[IDENTITY_SIZE];
/* A friend's first message can reach the host before Steam has told the host that friend is in the
 * lobby. Refusing for good then left the friend sending into nothing, so the host keeps asking. */
static uint64_t pending[PENDING_MAX];
static DWORD pending_since[PENDING_MAX];
static uint64_t asked[ASKED_MAX]; /* friends' lobbies whose data was requested */
static int last_send_result = RESULT_OK;
static bool got_message;

static int(__cdecl *p_InitFlat)(char *);
static int32_t(__cdecl *p_GetHSteamPipe)(void);
static void(__cdecl *p_ManualDispatch_Init)(void);
static void(__cdecl *p_ManualDispatch_RunFrame)(int32_t);
static bool(__cdecl *p_ManualDispatch_GetNextCallback)(int32_t, CallbackMsg *);
static void(__cdecl *p_ManualDispatch_FreeLastCallback)(int32_t);
static bool(__cdecl *p_ManualDispatch_GetAPICallResult)(int32_t, uint64_t, void *, int, int, bool *);
static void *(__cdecl *p_SteamUser)(void);
static void *(__cdecl *p_SteamMatchmaking)(void);
static void *(__cdecl *p_SteamFriends)(void);
static void *(__cdecl *p_SteamNetworkingMessages)(void);
static uint64_t(__cdecl *p_GetSteamID)(void *);
static uint64_t(__cdecl *p_CreateLobby)(void *, int, int);
static uint64_t(__cdecl *p_JoinLobby)(void *, uint64_t);
static void(__cdecl *p_LeaveLobby)(void *, uint64_t);
static bool(__cdecl *p_SetLobbyData)(void *, uint64_t, const char *, const char *);
static const char *(__cdecl *p_GetLobbyData)(void *, uint64_t, const char *);
static uint64_t(__cdecl *p_GetLobbyOwner)(void *, uint64_t);
static int(__cdecl *p_GetNumLobbyMembers)(void *, uint64_t);
static uint64_t(__cdecl *p_GetLobbyMemberByIndex)(void *, uint64_t, int);
static bool(__cdecl *p_SetRichPresence)(void *, const char *, const char *);
static void(__cdecl *p_ClearRichPresence)(void *);
static int(__cdecl *p_GetFriendCount)(void *, int);
static uint64_t(__cdecl *p_GetFriendByIndex)(void *, int, int);
static bool(__cdecl *p_GetFriendGamePlayed)(void *, uint64_t, void *);
static const char *(__cdecl *p_GetFriendPersonaName)(void *, uint64_t);
static int(__cdecl *p_SendMessageToUser)(void *, const void *, const void *, uint32_t, int, int);
static int(__cdecl *p_ReceiveMessagesOnChannel)(void *, int, void **, int);
static bool(__cdecl *p_AcceptSessionWithUser)(void *, const void *);
static bool(__cdecl *p_CloseSessionWithUser)(void *, const void *);
static void(__cdecl *p_SetSteamID64)(void *, uint64_t);
static uint64_t(__cdecl *p_GetSteamID64)(const void *);
static void(__cdecl *p_MessageRelease)(void *);
static int(__cdecl *p_GetFriendPersonaState)(void *, uint64_t);
static bool(__cdecl *p_InviteUserToLobby)(void *, uint64_t, uint64_t);
static bool(__cdecl *p_RequestLobbyData)(void *, uint64_t);

static bool load_symbol(void **slot, const char *name) {
    *slot = (void *)GetProcAddress(api, name);
    if (!*slot) say("steam: steam_api.dll has no %s, so it is too old or not the right file", name);
    return *slot != NULL;
}

static bool load_api(void) {
    char path[MAX_PATH], *slash;
    GetModuleFileNameA(NULL, path, sizeof path);
    slash = strrchr(path, '\\');
    if (slash) slash[1] = 0;
    strncat(path, "steam_api.dll", sizeof path - strlen(path) - 1);
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        say("steam: no steam_api.dll next to the exe, so Steam is off. See the README to get it");
        set_off("STEAM OFF: NO STEAM_API.DLL");
        return false;
    }
    api = LoadLibraryA(path);
    if (!api) {
        DWORD error = GetLastError();
        say("steam: steam_api.dll is there but did not load (%lu)%s", error, error == ERROR_BAD_EXE_FORMAT ? ", it is the 64-bit one and the game needs Windows-x86/steam_api.dll" : "");
        set_off(error == ERROR_BAD_EXE_FORMAT ? "STEAM OFF: USE 32-BIT STEAM_API.DLL" : "STEAM OFF: STEAM_API.DLL DIDN'T LOAD");
        return false;
    }
    bool ok = true;
#define SYM(var, name) ok = load_symbol((void **)&var, name) && ok
    SYM(p_InitFlat, "SteamAPI_InitFlat");
    SYM(p_GetHSteamPipe, "SteamAPI_GetHSteamPipe");
    SYM(p_ManualDispatch_Init, "SteamAPI_ManualDispatch_Init");
    SYM(p_ManualDispatch_RunFrame, "SteamAPI_ManualDispatch_RunFrame");
    SYM(p_ManualDispatch_GetNextCallback, "SteamAPI_ManualDispatch_GetNextCallback");
    SYM(p_ManualDispatch_FreeLastCallback, "SteamAPI_ManualDispatch_FreeLastCallback");
    SYM(p_ManualDispatch_GetAPICallResult, "SteamAPI_ManualDispatch_GetAPICallResult");
    SYM(p_SteamUser, "SteamAPI_SteamUser_v023");
    SYM(p_SteamMatchmaking, "SteamAPI_SteamMatchmaking_v009");
    SYM(p_SteamFriends, "SteamAPI_SteamFriends_v018");
    SYM(p_SteamNetworkingMessages, "SteamAPI_SteamNetworkingMessages_SteamAPI_v002");
    SYM(p_GetSteamID, "SteamAPI_ISteamUser_GetSteamID");
    SYM(p_CreateLobby, "SteamAPI_ISteamMatchmaking_CreateLobby");
    SYM(p_JoinLobby, "SteamAPI_ISteamMatchmaking_JoinLobby");
    SYM(p_LeaveLobby, "SteamAPI_ISteamMatchmaking_LeaveLobby");
    SYM(p_SetLobbyData, "SteamAPI_ISteamMatchmaking_SetLobbyData");
    SYM(p_GetLobbyData, "SteamAPI_ISteamMatchmaking_GetLobbyData");
    SYM(p_GetLobbyOwner, "SteamAPI_ISteamMatchmaking_GetLobbyOwner");
    SYM(p_GetNumLobbyMembers, "SteamAPI_ISteamMatchmaking_GetNumLobbyMembers");
    SYM(p_GetLobbyMemberByIndex, "SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex");
    SYM(p_SetRichPresence, "SteamAPI_ISteamFriends_SetRichPresence");
    SYM(p_ClearRichPresence, "SteamAPI_ISteamFriends_ClearRichPresence");
    SYM(p_GetFriendCount, "SteamAPI_ISteamFriends_GetFriendCount");
    SYM(p_GetFriendByIndex, "SteamAPI_ISteamFriends_GetFriendByIndex");
    SYM(p_GetFriendGamePlayed, "SteamAPI_ISteamFriends_GetFriendGamePlayed");
    SYM(p_GetFriendPersonaName, "SteamAPI_ISteamFriends_GetFriendPersonaName");
    SYM(p_SendMessageToUser, "SteamAPI_ISteamNetworkingMessages_SendMessageToUser");
    SYM(p_ReceiveMessagesOnChannel, "SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel");
    SYM(p_AcceptSessionWithUser, "SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser");
    SYM(p_CloseSessionWithUser, "SteamAPI_ISteamNetworkingMessages_CloseSessionWithUser");
    SYM(p_SetSteamID64, "SteamAPI_SteamNetworkingIdentity_SetSteamID64");
    SYM(p_GetSteamID64, "SteamAPI_SteamNetworkingIdentity_GetSteamID64");
    SYM(p_MessageRelease, "SteamAPI_SteamNetworkingMessage_t_Release");
#undef SYM
    /* Newer additions: without them only invites and finding friends who launched from Steam stop. */
    load_symbol((void **)&p_GetFriendPersonaState, "SteamAPI_ISteamFriends_GetFriendPersonaState");
    load_symbol((void **)&p_InviteUserToLobby, "SteamAPI_ISteamMatchmaking_InviteUserToLobby");
    load_symbol((void **)&p_RequestLobbyData, "SteamAPI_ISteamMatchmaking_RequestLobbyData");
    if (!ok) set_off("STEAM OFF: WRONG STEAM_API.DLL");
    return ok;
}

static bool real_start(void) {
    char error[1024] = "", launched[32] = "";
    if (!load_api()) return false;
    /* Steam's Play button starts the game as FNAF 4 (388090) and loads its overlay. The mod still
     * runs as Spacewar, so both players' copies talk under the same app. */
    if (GetEnvironmentVariableA("SteamAppId", launched, sizeof launched) && strcmp(launched, APP_ID) != 0)
        say("steam: started by Steam as app %s, the mod runs as app %s", launched, APP_ID);
    say("steam: overlay renderer %s. FNAF 4 doesn't show the Steam overlay, so invite friends from the CO-OP panel", GetModuleHandleA("GameOverlayRenderer.dll") ? "loaded" : "not loaded");
    /* Steam reads the app id from the environment or from this file next to the exe. */
    SetEnvironmentVariableA("SteamAppId", APP_ID);
    SetEnvironmentVariableA("SteamGameId", APP_ID);
    char path[MAX_PATH], *slash;
    GetModuleFileNameA(NULL, path, sizeof path);
    slash = strrchr(path, '\\');
    if (slash) slash[1] = 0;
    strncat(path, "steam_appid.txt", sizeof path - strlen(path) - 1);
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        FILE *f = fopen(path, "w");
        if (f) {
            fputs(APP_ID, f);
            fclose(f);
        }
    }
    int result = p_InitFlat(error);
    if (result != 0) {
        say("steam: not started (%d%s%s). Is Steam for Windows running and signed in? On a Mac it must run in the same Wine setup", result, error[0] ? ": " : "", error);
        set_off(result == 2 ? "STEAM OFF: STEAM ISN'T RUNNING" : result == 3 ? "STEAM OFF: UPDATE STEAM" : "STEAM OFF: STEAM DIDN'T START");
        return false;
    }
    p_ManualDispatch_Init();
    pipe_handle = p_GetHSteamPipe();
    user = p_SteamUser();
    matchmaking = p_SteamMatchmaking();
    friends = p_SteamFriends();
    messages = p_SteamNetworkingMessages();
    if (!user || !matchmaking || !friends || !messages) {
        say("steam: an interface was missing (user %p, matchmaking %p, friends %p, messages %p)", user, matchmaking, friends, messages);
        set_off("STEAM OFF: WRONG STEAM_API.DLL");
        return false;
    }
    me = p_GetSteamID(user);
    join_request = lobby_from_text(GetCommandLineA()); /* Steam starts the game this way from an invite */
    return true;
}

static void *identity_of(uint64_t id) {
    memset(identity, 0, sizeof identity);
    p_SetSteamID64(identity, id);
    return identity;
}

static bool real_is_member(uint64_t id) {
    int count = p_GetNumLobbyMembers(matchmaking, lobby);
    for (int i = 0; i < count; i++)
        if (p_GetLobbyMemberByIndex(matchmaking, lobby, i) == id) return true;
    return false;
}

/* Real Steam reports a new lobby twice, so only the first report counts. */
static bool lobby_opened(uint64_t id) {
    if (phase != PHASE_CREATING) return false;
    lobby = id;
    owner = me;
    phase = PHASE_IN;
    say("steam: lobby %llu is open to friends", (unsigned long long)lobby);
    return true;
}

static void lobby_created(int32_t result, uint64_t id) {
    char text[32];
    if (phase != PHASE_CREATING) return;
    if (result != RESULT_OK || !id) {
        phase = PHASE_NONE;
        set_failure("Steam could not create a lobby");
        return;
    }
    if (!lobby_opened(id)) return;
    snprintf(text, sizeof text, "%d", SYNC_VERSION);
    p_SetLobbyData(matchmaking, lobby, LOBBY_KEY, text);
    p_SetLobbyData(matchmaking, lobby, "name", room_name);
    snprintf(text, sizeof text, "+connect_lobby %llu", (unsigned long long)lobby);
    p_SetRichPresence(friends, "connect", text);
    p_SetRichPresence(friends, "status", "Hosting FNAF 4 co-op");
}

static void lobby_entered(uint64_t id, uint32_t response) {
    char version[16];
    if (phase != PHASE_JOINING) return;
    if (response != 1) {
        phase = PHASE_NONE;
        say("steam: Steam answered %u to entering lobby %llu", response, (unsigned long long)id);
        set_failure(response == 2 ? "That Steam room is gone" : response == 4 ? "That Steam room is full" : response == 3 ? "That Steam room is for friends of the host only" : response == 7 ? "Steam won't let a limited account join rooms" : "Couldn't join that Steam room. It may have closed");
        return;
    }
    const char *mark = p_GetLobbyData(matchmaking, id, LOBBY_KEY);
    snprintf(version, sizeof version, "%d", SYNC_VERSION);
    if (!mark || !mark[0] || strcmp(mark, version) != 0) {
        p_LeaveLobby(matchmaking, id);
        phase = PHASE_NONE;
        set_failure(mark && mark[0] ? "That Steam room is on another version of the mod" : "That Steam lobby isn't an FNAF 4 co-op room");
        return;
    }
    lobby = id;
    owner = p_GetLobbyOwner(matchmaking, id);
    phase = PHASE_IN;
    say("steam: joined lobby %llu, hosted by %llu", (unsigned long long)lobby, (unsigned long long)owner);
}

static void pending_add(uint64_t id) {
    int slot = -1;
    for (int i = 0; i < PENDING_MAX; i++) {
        if (pending[i] == id) slot = i;
        else if (!pending[i] && slot < 0) slot = i;
    }
    if (slot < 0) slot = 0;
    pending[slot] = id;
    pending_since[slot] = GetTickCount();
}

/* Sessions from friends whose lobby entry hadn't reached this host yet. */
static void pending_check(void) {
    DWORD now = GetTickCount();
    for (int i = 0; i < PENDING_MAX; i++) {
        if (!pending[i]) continue;
        if (lobby && owner == me && real_is_member(pending[i])) {
            p_AcceptSessionWithUser(messages, identity_of(pending[i]));
            say("steam: accepted a session from %llu once it was in the lobby", (unsigned long long)pending[i]);
            pending[i] = 0;
        } else if (now - pending_since[i] > PENDING_MS) {
            say("steam: %llu never showed up in the lobby, its session stays closed", (unsigned long long)pending[i]);
            pending[i] = 0;
        }
    }
}

static void handle_callback(int32_t id, const uint8_t *param, int size) {
    switch (id) {
    case CB_LOBBY_CREATED: lobby_created(*(const int32_t *)param, *(const uint64_t *)(param + 8)); break;
    case CB_LOBBY_ENTER: lobby_entered(*(const uint64_t *)param, *(const uint32_t *)(param + 16)); break;
    case CB_LOBBY_JOIN_REQUESTED:
        join_request = *(const uint64_t *)param;
        say("steam: the player accepted an invite to lobby %llu", (unsigned long long)join_request);
        break;
    case CB_RICH_PRESENCE_JOIN_REQUESTED:
        join_request = lobby_from_text((const char *)param + 8);
        say("steam: the player clicked Join Game, lobby %llu", (unsigned long long)join_request);
        break;
    case CB_LOBBY_CHAT_UPDATE: {
        uint64_t changed = *(const uint64_t *)(param + 8);
        uint32_t state = *(const uint32_t *)(param + 24);
        if (*(const uint64_t *)param == lobby) say("steam: %llu %s the lobby", (unsigned long long)changed, state & 1 ? "entered" : "left");
        break;
    }
    case CB_SESSION_REQUEST: {
        uint64_t from = p_GetSteamID64(param);
        /* The host lets in its lobby's members, a client only its host. */
        bool host = lobby && owner == me;
        bool welcome = lobby && (host ? real_is_member(from) : from == owner);
        if (welcome) p_AcceptSessionWithUser(messages, param);
        else if (host) pending_add(from);
        say("steam: %s a session from %llu", welcome ? "accepted" : host ? "holding, not in the lobby yet," : "refused", (unsigned long long)from);
        break;
    }
    case CB_SESSION_FAILED: {
        /* SteamNetConnectionInfo_t: end reason at 180 and its text at 184, by the SDK headers. */
        char why[129] = "";
        int reason = size >= 312 ? *(const int32_t *)(param + 180) : 0;
        if (size >= 312) {
            memcpy(why, param + 184, 128);
            why[128] = 0;
            for (char *c = why; *c; c++)
                if (*c < 32 || *c > 126) *c = '?';
        }
        say("steam: the session with %llu failed (%d %s)", (unsigned long long)p_GetSteamID64(param), reason, why);
        break;
    }
    default: break;
    }
}

static void real_tick(void) {
    CallbackMsg msg;
    p_ManualDispatch_RunFrame(pipe_handle);
    pending_check();
    while (p_ManualDispatch_GetNextCallback(pipe_handle, &msg)) {
        if (msg.callback == CB_CALL_COMPLETED) {
            uint64_t call = *(const uint64_t *)msg.param;
            int32_t expected = *(const int32_t *)(msg.param + 8);
            uint32_t size = *(const uint32_t *)(msg.param + 12);
            void *result = size ? calloc(1, size) : NULL;
            bool failed = true;
            if (result && p_ManualDispatch_GetAPICallResult(pipe_handle, call, result, (int)size, expected, &failed) && !failed) {
                if (call == create_call || call == join_call) handle_callback(expected, result, (int)size);
            }
            free(result);
        } else {
            handle_callback(msg.callback, msg.param, msg.size);
        }
        p_ManualDispatch_FreeLastCallback(pipe_handle);
    }
}

static int real_receive(uint64_t *from, void *buffer, int capacity) {
    void *message = NULL;
    if (p_ReceiveMessagesOnChannel(messages, 0, &message, 1) < 1 || !message) return -1;
    const uint8_t *m = message;
    const void *data = *(void *const *)m;
    int size = *(const int32_t *)(m + 4);
    if (!sender_offset) {
        sender_offset = p_GetSteamID64(m + 12) ? 12 : 16;
        say("steam: message sender found at offset %d", sender_offset);
    }
    *from = p_GetSteamID64(m + sender_offset);
    if (!got_message) say("steam: first message, from %llu", (unsigned long long)*from);
    got_message = true;
    if (size > capacity) size = -2;
    else memcpy(buffer, data, (size_t)size);
    p_MessageRelease(message);
    return size;
}

static int real_friend_rooms(SteamRoom *out, int max) {
    uint8_t info[32];
    int count = p_GetFriendCount(friends, FRIEND_FLAG_IMMEDIATE), found = 0;
    for (int i = 0; i < count && found < max; i++) {
        uint64_t friend_id = p_GetFriendByIndex(friends, i, FRIEND_FLAG_IMMEDIATE);
        memset(info, 0, sizeof info);
        if (!p_GetFriendGamePlayed(friends, friend_id, info)) continue;
        uint64_t game = *(const uint64_t *)info, room = *(const uint64_t *)(info + 16);
        if (!room) continue;
        /* A friend who started the game with Steam's Play button can show as FNAF 4 rather than
         * Spacewar, so the lobby's own mark decides. Steam fetches it for lobbies we're not in. */
        const char *mark = p_GetLobbyData(matchmaking, room, LOBBY_KEY);
        if (!mark || !mark[0]) {
            bool seen = false;
            for (int k = 0; k < ASKED_MAX; k++) seen = seen || asked[k] == room;
            if (!seen) {
                memmove(asked + 1, asked, sizeof asked - sizeof asked[0]);
                asked[0] = room;
                if (p_RequestLobbyData) p_RequestLobbyData(matchmaking, room);
            }
            if ((game & 0xFFFFFF) != 480) continue;
        }
        out[found].lobby = room;
        out[found].owner = friend_id;
        snprintf(out[found].name, STEAM_NAME_LEN, "%s", p_GetFriendPersonaName(friends, friend_id));
        found++;
    }
    return found;
}

/* ---------------------------------------------------------------- test stand-in */

#define MOCK_BASE 76561198860265728ULL /* 76561197960265728 + 900000000, a reserved test range */
#define MOCK_PORT 47300
#define MOCK_KNOWN 16
#define MOCK_PATH 400

static SteamOptions opts;
static SOCKET mock_socket = INVALID_SOCKET;
static uint64_t mock_known[MOCK_KNOWN];
static uint32_t mock_last_join_check;

static void mock_lobby_path(uint64_t id, char *out, size_t cap) { snprintf(out, cap, "%s\\lobby-%llu.ini", opts.mock_dir, (unsigned long long)id); }

static bool mock_knows(uint64_t id) {
    for (int i = 0; i < MOCK_KNOWN; i++)
        if (mock_known[i] == id) return true;
    return false;
}

static void mock_know(uint64_t id) {
    for (int i = 0; i < MOCK_KNOWN; i++)
        if (!mock_known[i] || mock_known[i] == id) {
            mock_known[i] = id;
            return;
        }
}

static bool mock_start(void) {
    u_long nonblocking = 1;
    struct sockaddr_in at;
    me = MOCK_BASE + (uint64_t)opts.mock_slot;
    CreateDirectoryA(opts.mock_dir, NULL);
    mock_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (mock_socket == INVALID_SOCKET) return false;
    ioctlsocket(mock_socket, FIONBIO, &nonblocking);
    memset(&at, 0, sizeof at);
    at.sin_family = AF_INET;
    at.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    at.sin_port = htons((u_short)(MOCK_PORT + opts.mock_slot));
    if (bind(mock_socket, (struct sockaddr *)&at, sizeof at) != 0) {
        say("steam stand-in: cannot bind port %d", MOCK_PORT + opts.mock_slot);
        return false;
    }
    say("steam stand-in: player %llu, lobbies in %s", (unsigned long long)me, opts.mock_dir);
    return true;
}

static bool mock_is_member(uint64_t id) {
    char path[MOCK_PATH], key[32];
    mock_lobby_path(lobby, path, sizeof path);
    snprintf(key, sizeof key, "m%llu", (unsigned long long)id);
    return GetPrivateProfileIntA("lobby", key, 0, path) != 0;
}

static void mock_host(void) {
    char path[MOCK_PATH], text[32];
    uint64_t id = 0x0186000000000000ULL | (uint64_t)opts.mock_slot;
    mock_lobby_path(id, path, sizeof path);
    DeleteFileA(path);
    snprintf(text, sizeof text, "%llu", (unsigned long long)me);
    WritePrivateProfileStringA("lobby", "owner", text, path);
    WritePrivateProfileStringA("lobby", "name", room_name, path);
    snprintf(text, sizeof text, "%d", SYNC_VERSION);
    WritePrivateProfileStringA("lobby", LOBBY_KEY, text, path);
    /* Real Steam reports the new lobby twice, so the stand-in does too. */
    lobby_opened(id);
    lobby_opened(id);
}

static void mock_join(uint64_t id) {
    char path[MOCK_PATH], text[32], version[16], key[32];
    mock_lobby_path(id, path, sizeof path);
    GetPrivateProfileStringA("lobby", "owner", "", text, sizeof text, path);
    if (!text[0]) {
        phase = PHASE_NONE;
        set_failure("Couldn't join that Steam room. It may have closed");
        return;
    }
    GetPrivateProfileStringA("lobby", LOBBY_KEY, "", version, sizeof version, path);
    snprintf(key, sizeof key, "%d", SYNC_VERSION);
    if (strcmp(version, key) != 0) {
        phase = PHASE_NONE;
        set_failure(version[0] ? "That Steam room is on another version of the mod" : "That Steam lobby isn't an FNAF 4 co-op room");
        return;
    }
    snprintf(key, sizeof key, "m%llu", (unsigned long long)me);
    WritePrivateProfileStringA("lobby", key, "1", path);
    lobby = id;
    owner = _strtoui64(text, NULL, 10);
    phase = PHASE_IN;
    say("steam: joined lobby %llu, hosted by %llu", (unsigned long long)lobby, (unsigned long long)owner);
}

static void mock_leave(void) {
    char path[MOCK_PATH], key[32];
    mock_lobby_path(lobby, path, sizeof path);
    if (owner == me) DeleteFileA(path);
    else {
        snprintf(key, sizeof key, "m%llu", (unsigned long long)me);
        WritePrivateProfileStringA("lobby", key, NULL, path);
    }
}

static int mock_friend_rooms(SteamRoom *out, int max) {
    WIN32_FIND_DATAA found;
    char pattern[MOCK_PATH], path[MOCK_PATH], text[32];
    int count = 0;
    snprintf(pattern, sizeof pattern, "%s\\lobby-*.ini", opts.mock_dir);
    HANDLE h = FindFirstFileA(pattern, &found);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        uint64_t id = _strtoui64(found.cFileName + 6, NULL, 10);
        mock_lobby_path(id, path, sizeof path);
        GetPrivateProfileStringA("lobby", "owner", "", text, sizeof text, path);
        uint64_t host_id = _strtoui64(text, NULL, 10);
        if (!host_id || host_id == me || count >= max) continue;
        out[count].lobby = id;
        out[count].owner = host_id;
        GetPrivateProfileStringA("lobby", "name", "Friend", out[count].name, STEAM_NAME_LEN, path);
        count++;
    } while (FindNextFileA(h, &found));
    FindClose(h);
    return count;
}

/* An invite is a file named for the friend, holding the lobby. The friend's stand-in accepts it. */
static void mock_invite_path(uint64_t to, char *out, size_t cap) { snprintf(out, cap, "%s\\invite-%llu.ini", opts.mock_dir, (unsigned long long)to); }

static bool mock_invite(uint64_t to) {
    char path[MOCK_PATH], text[32];
    mock_invite_path(to, path, sizeof path);
    snprintf(text, sizeof text, "%llu", (unsigned long long)lobby);
    return WritePrivateProfileStringA("invite", "lobby", text, path) != 0;
}

static int mock_online_friends(SteamFriend *out, int max) {
    int count = 0;
    for (int slot = 1; slot <= 4 && count < max; slot++) {
        uint64_t id = MOCK_BASE + (uint64_t)slot;
        if (id == me) continue;
        out[count].id = id;
        snprintf(out[count].name, STEAM_NAME_LEN, "Stand-in %d", slot);
        count++;
    }
    return count;
}

static void mock_tick(uint32_t now) {
    char path[MOCK_PATH], text[32];
    if (join_request || lobby || now - mock_last_join_check < 1000) return;
    mock_last_join_check = now;
    mock_invite_path(me, path, sizeof path);
    GetPrivateProfileStringA("invite", "lobby", "", text, sizeof text, path);
    if (text[0]) {
        DeleteFileA(path);
        join_request = _strtoui64(text, NULL, 10);
        say("steam: the player accepted an invite to lobby %llu", (unsigned long long)join_request);
        return;
    }
    if (!opts.mock_join) return;
    SteamRoom room;
    if (mock_friend_rooms(&room, 1) == 1) {
        join_request = room.lobby;
        opts.mock_join = false;
        say("steam stand-in: clicked Join Game on %s", room.name);
    }
}

static bool mock_send(uint64_t to, const void *data, int size) {
    uint8_t packet[1400];
    struct sockaddr_in at;
    if (size < 0 || size + 8 > (int)sizeof packet || to < MOCK_BASE || to > MOCK_BASE + 64) return false;
    memcpy(packet, &me, 8);
    memcpy(packet + 8, data, (size_t)size);
    memset(&at, 0, sizeof at);
    at.sin_family = AF_INET;
    at.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    at.sin_port = htons((u_short)(MOCK_PORT + (to - MOCK_BASE)));
    mock_know(to); /* sending opens our side of the session, as on Steam */
    return sendto(mock_socket, (const char *)packet, size + 8, 0, (struct sockaddr *)&at, sizeof at) == size + 8;
}

static int mock_receive(uint64_t *from, void *buffer, int capacity) {
    uint8_t packet[1400];
    for (;;) {
        int n = recv(mock_socket, (char *)packet, sizeof packet, 0);
        if (n < 8) return -1;
        memcpy(from, packet, 8);
        if (!mock_knows(*from)) {
            /* Like Steam, a stranger's messages wait for an accept; here they are dropped meanwhile. */
            bool welcome = lobby && (owner == me ? mock_is_member(*from) : *from == owner);
            say("steam: %s a session from %llu", welcome ? "accepted" : "refused", (unsigned long long)*from);
            if (!welcome) continue;
            mock_know(*from);
            continue;
        }
        if (n - 8 > capacity) return -2;
        memcpy(buffer, packet + 8, (size_t)(n - 8));
        return n - 8;
    }
}

/* ---------------------------------------------------------------- shared */

bool steam_start(const SteamOptions *options, SteamLog log) {
    say = log;
    opts = *options;
    mock = opts.mock_slot > 0;
    ready = mock ? mock_start() : real_start();
    if (ready) say("steam: ready as %llu%s", (unsigned long long)me, mock ? " (test stand-in)" : "");
    return ready;
}

bool steam_ready(void) { return ready; }
const char *steam_off_reason(void) { return ready ? "STEAM ON" : off_reason; }
uint64_t steam_me(void) { return me; }
uint64_t steam_lobby(void) { return phase == PHASE_IN ? lobby : 0; }
uint64_t steam_lobby_owner(void) { return phase == PHASE_IN ? owner : 0; }
const char *steam_failure(void) { return failure; }

void steam_tick(uint32_t now_ms) {
    if (!ready) return;
    if (mock) mock_tick(now_ms);
    else real_tick();
}

uint64_t steam_take_join_request(void) {
    uint64_t id = join_request;
    join_request = 0;
    return id;
}

void steam_host(const char *name) {
    if (!ready) return;
    steam_leave();
    failure[0] = 0;
    snprintf(room_name, sizeof room_name, "%s", name);
    phase = PHASE_CREATING;
    if (mock) mock_host();
    else create_call = p_CreateLobby(matchmaking, LOBBY_FRIENDS_ONLY, LOBBY_MAX_MEMBERS);
}

void steam_join(uint64_t id) {
    if (!ready || !id) return;
    steam_leave();
    failure[0] = 0;
    phase = PHASE_JOINING;
    say("steam: joining lobby %llu", (unsigned long long)id);
    if (mock) mock_join(id);
    else join_call = p_JoinLobby(matchmaking, id);
}

void steam_leave(void) {
    if (!ready || !lobby) {
        phase = PHASE_NONE;
        return;
    }
    if (mock) mock_leave();
    else {
        if (owner && owner != me) p_CloseSessionWithUser(messages, identity_of(owner));
        p_LeaveLobby(matchmaking, lobby);
        p_ClearRichPresence(friends);
    }
    say("steam: left lobby %llu", (unsigned long long)lobby);
    memset(mock_known, 0, sizeof mock_known);
    memset(pending, 0, sizeof pending);
    lobby = owner = 0;
    phase = PHASE_NONE;
}

int steam_friend_rooms(SteamRoom *out, int max) {
    if (!ready) return 0;
    return mock ? mock_friend_rooms(out, max) : real_friend_rooms(out, max);
}

bool steam_send(uint64_t to, const void *data, int size) {
    if (!ready || !to) return false;
    if (mock) return mock_send(to, data, size);
    int result = p_SendMessageToUser(messages, identity_of(to), data, (uint32_t)size, SEND_FLAGS, 0);
    if (result != last_send_result) say("steam: sending to %llu now gives result %d (1 is OK)", (unsigned long long)to, result);
    last_send_result = result;
    return result == RESULT_OK;
}

static int real_online_friends(SteamFriend *out, int max) {
    uint8_t info[32];
    if (!p_GetFriendPersonaState || !p_InviteUserToLobby) return 0;
    int count = p_GetFriendCount(friends, FRIEND_FLAG_IMMEDIATE), found = 0;
    /* Two passes: friends in a game first, then everyone else online. */
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < count && found < max; i++) {
            uint64_t friend_id = p_GetFriendByIndex(friends, i, FRIEND_FLAG_IMMEDIATE);
            if (p_GetFriendPersonaState(friends, friend_id) == 0) continue; /* offline */
            memset(info, 0, sizeof info);
            bool playing = p_GetFriendGamePlayed(friends, friend_id, info);
            if (playing != (pass == 0)) continue;
            out[found].id = friend_id;
            snprintf(out[found].name, STEAM_NAME_LEN, "%s", p_GetFriendPersonaName(friends, friend_id));
            found++;
        }
    return found;
}

int steam_online_friends(SteamFriend *out, int max) {
    if (!ready) return 0;
    return mock ? mock_online_friends(out, max) : real_online_friends(out, max);
}

bool steam_invite(uint64_t friend_id) {
    if (!ready || phase != PHASE_IN || owner != me || !friend_id) return false;
    bool ok = mock ? mock_invite(friend_id) : p_InviteUserToLobby && p_InviteUserToLobby(matchmaking, lobby, friend_id);
    say("steam: invite to %llu %s", (unsigned long long)friend_id, ok ? "sent" : "failed");
    return ok;
}

int steam_receive(uint64_t *from, void *buffer, int capacity) {
    if (!ready) return -1;
    return mock ? mock_receive(from, buffer, capacity) : real_receive(from, buffer, capacity);
}
