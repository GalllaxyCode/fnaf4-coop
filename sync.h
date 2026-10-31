/* Shared-night sync: pure logic with no game or network access, so it can be tested on its own.
 * The host runs the simulation. Clients follow its frame and mirror its values, and every player's
 * input is merged so a door held by anyone counts as shut. */
#ifndef SYNC_H
#define SYNC_H
#include <stdbool.h>
#include <stdint.h>

#define SYNC_MAGIC 0x4F433446u /* "F4CO" */
#define SYNC_VERSION 2
#define SYNC_MAX_PEERS 7
#define SYNC_MAX_PLAYERS (SYNC_MAX_PEERS + 1)
#define SYNC_NAME_LEN 16
#define SYNC_VALUES 32
#define SYNC_PEER_TIMEOUT_MS 5000
#define SYNC_HOST_LOST_MS 25000 /* loading a night freezes the game for about ten seconds, so silence alone proves little */
#define SYNC_FOLLOW_GRACE_MS 250
#define SYNC_FOLLOW_KILL_GRACE_MS 5000 /* lets a jumpscare finish before a client is moved */
#define SYNC_FOLLOW_RETRY_MS 1500

/* Where a player is in the room. */
enum { STATION_NONE, STATION_CENTER, STATION_LEFT, STATION_RIGHT, STATION_CLOSET, STATION_BED };

/* Held input, reported every packet. */
enum {
    IN_HOLD_LEFT = 1 << 0,
    IN_HOLD_RIGHT = 1 << 1,
    IN_HOLD_CLOSET = 1 << 2,
    IN_LISTEN_LEFT = 1 << 3,
    IN_LISTEN_RIGHT = 1 << 4,
    IN_VIEW_LEFT = 1 << 5,
    IN_VIEW_RIGHT = 1 << 6,
    IN_VIEW_CLOSET = 1 << 7,
    IN_VIEW_BED = 1 << 8,
    IN_SIDE_LEFT = 1 << 9,
    IN_SIDE_RIGHT = 1 << 10,
    IN_LIGHT = 1 << 11
};

/* One-off actions that change the shared threats. */
enum { EV_SCARE_BONNIE, EV_SCARE_CHICA, EV_SCARE_FOXY_LEFT, EV_SCARE_FOXY_RIGHT, EV_MOVED, EV_COUNT };

enum { KILL_NONE, KILL_BONNIE, KILL_CHICA, KILL_FREDDY, KILL_FOXY, KILL_CUPCAKE, KILL_BONNIE_BED, KILL_FREDDY_BED, KILL_FREDBEAR, KILL_COUNT };

/* The game this tick. */
typedef struct {
    int frame; /* -1 before the runtime has a frame */
    bool in_level;
    int station;
    uint16_t input;
    int kill; /* set once this player has died in the current attempt */
    bool events[EV_COUNT];
    int value_count;
    int32_t values[SYNC_VALUES];
} Local;

typedef struct {
    bool used;
    int station;
    uint16_t input;
    char name[SYNC_NAME_LEN];
} Player;

/* What the mod must do this tick. */
typedef struct {
    bool go_to_frame;
    int frame;
    bool apply_values;
    int32_t values[SYNC_VALUES];
    uint16_t others; /* every other player's input, merged */
    int apply_kill; /* run this scare on this player */
    bool events[EV_COUNT]; /* host: other players did these */
    int player_count;
    Player players[SYNC_MAX_PLAYERS]; /* everyone but this player */
    bool host_lost;
} Output;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t is_host;
    uint8_t station;
    uint8_t kill;
    uint32_t session;
    uint32_t seq;
    uint32_t epoch;
    int32_t frame;
    uint16_t input;
    uint8_t in_level;
    uint8_t value_count;
    uint8_t event_counts[EV_COUNT];
    uint8_t player_count;
    uint8_t closing; /* the sender is leaving the room */
    uint8_t pad;
    char name[SYNC_NAME_LEN];
    int32_t values[SYNC_VALUES];
    struct {
        uint8_t station;
        uint8_t pad;
        uint16_t input;
        char name[SYNC_NAME_LEN];
    } players[SYNC_MAX_PLAYERS];
} Packet;

typedef struct {
    bool used;
    uint32_t session;
    uint32_t last_seq;
    uint32_t last_ms;
    int station;
    uint16_t input;
    int kill;
    uint32_t kill_epoch;
    char name[SYNC_NAME_LEN];
    bool have_events;
    uint8_t event_counts[EV_COUNT];
    bool pending_events[EV_COUNT];
} Peer;

typedef struct {
    bool is_host;
    uint32_t session;
    uint32_t seq;
    char name[SYNC_NAME_LEN];

    /* host */
    uint32_t epoch;
    int kill;
    bool was_in_level;
    Peer peers[SYNC_MAX_PEERS];

    /* client */
    bool have_host;
    uint32_t host_session;
    uint32_t host_seq;
    uint32_t host_ms;
    bool host_closed;
    Packet host;
    uint32_t my_epoch;
    uint32_t mismatch_ms;
    bool mismatch;
    uint32_t last_goto_ms;
    bool sent_goto;
    uint8_t event_counts[EV_COUNT];
    bool kill_applied;
} Sync;

void sync_init(Sync *s, bool is_host, uint32_t session, const char *name);
/* Returns false when the packet is rejected. peer is the sender's slot on the host, ignored on clients. */
bool sync_recv(Sync *s, int peer, const Packet *p, uint32_t now_ms);
void sync_step(Sync *s, uint32_t now_ms, const Local *l, Output *out, Packet *send);

#endif
