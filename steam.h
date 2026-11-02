/* Steam lobbies and relayed messages, so friends on different networks can play without typing an
 * address. Runs as Valve's free sample app 480 "Spacewar", which every Steam account can use. It
 * loads steam_api.dll at run time, so without the DLL or a running Steam nothing changes.
 *
 * A test stand-in replaces Steam when fnaf4coop.ini asks for it: one account per test copy, lobbies
 * as files in a folder both copies share, and messages over localhost. */
#ifndef STEAM_H
#define STEAM_H
#include <stdbool.h>
#include <stdint.h>

#define STEAM_NAME_LEN 32
#define STEAM_MAX_FRIEND_ROOMS 8

typedef struct {
    uint64_t lobby;
    uint64_t owner;
    char name[STEAM_NAME_LEN];
} SteamRoom;

typedef struct {
    int mock_slot; /* above 0: use the test stand-in as this player */
    char mock_dir[260]; /* the folder the stand-in copies share */
    bool mock_join; /* the stand-in clicks Join Game on the first friend's room it sees */
} SteamOptions;

typedef void (*SteamLog)(const char *fmt, ...);

/* Starts Steam if it is there. Returns false, and changes nothing, when it isn't. */
bool steam_start(const SteamOptions *options, SteamLog log);
bool steam_ready(void);
void steam_tick(uint32_t now_ms);
uint64_t steam_me(void);

/* Lobbies. One at a time: hosting or joining replaces the last one. */
void steam_host(const char *room_name);
void steam_join(uint64_t lobby);
void steam_leave(void);
uint64_t steam_lobby(void); /* 0 until the lobby is created or entered */
uint64_t steam_lobby_owner(void);
const char *steam_failure(void); /* set when creating or joining failed, until the next try */
uint64_t steam_take_join_request(void); /* a lobby the player chose in Steam, once */
int steam_friend_rooms(SteamRoom *out, int max); /* friends hosting a co-op room */

/* Unreliable messages to and from other players, by Steam account. */
bool steam_send(uint64_t to, const void *data, int size);
int steam_receive(uint64_t *from, void *buffer, int capacity); /* -1 when there is nothing */

#endif
