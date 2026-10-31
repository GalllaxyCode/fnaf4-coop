/* Linked-nights sync: pure logic with no game or network access, so it can be tested on its own. */
#ifndef SYNC_H
#define SYNC_H
#include <stdbool.h>
#include <stdint.h>

#define SYNC_MAGIC 0x4F433446u /* "F4CO" */
#define SYNC_VERSION 1
#define SYNC_MAX_PEERS 7
#define SYNC_PEER_TIMEOUT_MS 5000
#define SYNC_WIN_HOUR 6
#define SYNC_EXIT_WAIT_MS 5000 /* how long a frame change may take before leaving counts as quitting */

enum { RESULT_NONE = 0, RESULT_DEAD = 1, RESULT_WON = 2, RESULT_ABORTED = 3 };

/* What the game looks like this tick. */
typedef struct {
    bool in_level;
    int night;
    int hour;
    int gameover;
    bool won_screen; /* the game is showing its night-win frame */
} World;

/* What the mod must write into the game this tick. */
typedef struct {
    bool set_hour;
    int hour;
    bool force_death; /* hold `Freddy counter` at 80 so the game runs its own kill */
} Writes;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t is_host;
    uint8_t in_level;
    uint8_t night;
    uint32_t session;
    uint32_t seq;
    uint32_t epoch; /* host's level counter; 0 before the first level */
    uint8_t hour;
    uint8_t result;
    uint8_t dead;
    uint8_t pad;
} Packet;

typedef struct {
    bool used;
    uint32_t session;
    uint32_t last_seq;
    uint32_t last_ms;
    bool dead;
    uint32_t dead_epoch;
} Peer;

typedef struct {
    bool is_host;
    uint32_t session;
    uint32_t seq;

    /* host */
    uint32_t epoch;
    int result;
    bool was_in_level;
    int last_hour;
    int last_night;
    bool exit_pending;
    uint32_t exit_ms;
    Peer peers[SYNC_MAX_PEERS];

    /* client */
    bool have_host;
    uint32_t host_session;
    uint32_t host_seq;
    uint32_t host_ms;
    Packet host;
    uint32_t my_epoch;
    bool my_dead;
    bool night_mismatch;
} Sync;

void sync_init(Sync *s, bool is_host, uint32_t session);
/* Returns false when the packet is rejected. peer is the slot of the sender on the host, ignored on clients. */
bool sync_recv(Sync *s, int peer, const Packet *p, uint32_t now_ms);
void sync_step(Sync *s, uint32_t now_ms, const World *w, Writes *out, Packet *send);

#endif
