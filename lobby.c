#include "lobby.h"
#include <string.h>

void lobby_clear(Lobby *l) { memset(l, 0, sizeof *l); }

bool lobby_note(Lobby *l, const Announce *a, uint32_t address, uint32_t now_ms) {
    Room *slot = NULL;
    if (a->magic != LOBBY_MAGIC || a->version != SYNC_VERSION) return false;
    for (int i = 0; i < LOBBY_MAX_ROOMS; i++)
        if (l->rooms[i].used && l->rooms[i].session == a->session) slot = &l->rooms[i];
    for (int i = 0; i < LOBBY_MAX_ROOMS && !slot; i++)
        if (!l->rooms[i].used) slot = &l->rooms[i];
    if (!slot) return false;
    slot->used = true;
    slot->session = a->session;
    slot->address = address;
    slot->port = a->port;
    slot->players = a->players;
    slot->seen_ms = now_ms;
    memcpy(slot->name, a->name, SYNC_NAME_LEN);
    slot->name[SYNC_NAME_LEN - 1] = 0;
    lobby_clean_name(slot->name, SYNC_NAME_LEN);
    return true;
}

void lobby_expire(Lobby *l, uint32_t now_ms) {
    for (int i = 0; i < LOBBY_MAX_ROOMS; i++)
        if (l->rooms[i].used && now_ms - l->rooms[i].seen_ms >= LOBBY_ROOM_TIMEOUT_MS) l->rooms[i].used = false;
}

int lobby_count(const Lobby *l) {
    int n = 0;
    for (int i = 0; i < LOBBY_MAX_ROOMS; i++) n += l->rooms[i].used;
    return n;
}

const Room *lobby_room(const Lobby *l, int index) {
    for (int i = 0; i < LOBBY_MAX_ROOMS; i++)
        if (l->rooms[i].used && index-- == 0) return &l->rooms[i];
    return NULL;
}

uint32_t lobby_sweep_address(uint32_t address, uint32_t mask, int step) {
    if (mask == 0 || mask == 0xFFFFFFFFu) mask = 0xFFFFFF00u; /* no usable mask: assume the common size */
    if (step < 0 || step >= LOBBY_SWEEP_MAX) return 0;
    if (step == 0) return address;
    uint32_t network = address & mask;
    int64_t below = (int64_t)address - network - 1, above = (int64_t)(network | ~mask) - address - 1;
    int64_t both = below < above ? below : above;
    if (both < 0) both = 0;
    /* Up one, down one, until a side runs out, then the rest of the other side. */
    if (step <= 2 * both) return step % 2 ? address + (uint32_t)((step + 1) / 2) : address - (uint32_t)(step / 2);
    int64_t rest = step - 2 * both;
    if (below > above) return rest <= below - both ? (uint32_t)(address - both - rest) : 0;
    return rest <= above - both ? (uint32_t)(address + both + rest) : 0;
}

void lobby_clean_name(char *name, int capacity) {
    int out = 0;
    for (int i = 0; i < capacity && name[i]; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c < 32 || c > 126) continue;
        if (c == ' ' && out == 0) continue;
        name[out++] = (char)c;
    }
    while (out > 0 && name[out - 1] == ' ') out--;
    name[out < capacity ? out : capacity - 1] = 0;
    if (out == 0) strncpy(name, "Player", (size_t)capacity - 1);
}
