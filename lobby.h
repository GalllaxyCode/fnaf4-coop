/* Rooms found on the local network. No game or network access, so it can be tested on its own. */
#ifndef LOBBY_H
#define LOBBY_H
#include <stdbool.h>
#include <stdint.h>
#include "sync.h"

#define LOBBY_MAGIC 0x4D523446u /* "F4RM" */
#define LOBBY_PORT 47115
#define LOBBY_MAX_ROOMS 4
#define LOBBY_ROOM_TIMEOUT_MS 3500
#define LOBBY_ANNOUNCE_MS 1000
#define LOBBY_ADDRESS_LEN 40

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t players;
    uint16_t port;
    uint32_t session;
    char name[SYNC_NAME_LEN];
} Announce;

typedef struct {
    bool used;
    uint32_t session;
    uint32_t address; /* network byte order */
    uint16_t port;
    int players;
    uint32_t seen_ms;
    char name[SYNC_NAME_LEN];
} Room;

typedef struct {
    Room rooms[LOBBY_MAX_ROOMS];
} Lobby;

void lobby_clear(Lobby *l);
bool lobby_note(Lobby *l, const Announce *a, uint32_t address, uint32_t now_ms);
void lobby_expire(Lobby *l, uint32_t now_ms);
int lobby_count(const Lobby *l);
const Room *lobby_room(const Lobby *l, int index);
/* Keeps printable ASCII, trims the ends, and falls back to "Player". */
void lobby_clean_name(char *name, int capacity);

#endif
