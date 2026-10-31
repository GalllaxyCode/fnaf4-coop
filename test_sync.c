/* Fake-world tests for sync.c: one host and several clients over a lossy, reordering network. */
#include "sync.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PLAYERS 4
#define QUEUE 4096
typedef struct { int to, from; uint32_t at; Packet p; bool used; } Msg;
typedef struct {
    Sync s; World w; int deaths_forced; bool won; bool dead; int freddy; int kill_in; uint32_t local_hour_ms; bool done;
} Player;
static Player pl[PLAYERS];
static Msg q[QUEUE];
static uint32_t now;
static int loss_pct, jitter_ms;
static int failures, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); puts(""); } } while (0)

static void net_send(int from, int to, const Packet *p) {
    if (rand() % 100 < loss_pct) return;
    for (int copies = rand() % 20 == 0 ? 2 : 1; copies > 0; copies--)
        for (int i = 0; i < QUEUE; i++)
            if (!q[i].used) {
                q[i] = (Msg){to, from, now + 5 + (jitter_ms ? (uint32_t)(rand() % jitter_ms) : 0), *p, true};
                break;
            }
}
static void net_pump(void) {
    for (int i = 0; i < QUEUE; i++)
        if (q[i].used && q[i].at <= now) {
            sync_recv(&pl[q[i].to].s, q[i].from - 1, &q[i].p, now);
            q[i].used = false;
        }
}
static void reset(int night) {
    memset(q, 0, sizeof q);
    for (int i = 0; i < PLAYERS; i++) {
        sync_init(&pl[i].s, i == 0, 1000 + (uint32_t)i);
        pl[i].w = (World){true, night, 12, 0, false};
        pl[i].deaths_forced = pl[i].freddy = pl[i].kill_in = 0;
        pl[i].won = pl[i].dead = pl[i].done = false;
        pl[i].local_hour_ms = 0;
    }
}
/* Mirrors the level sheet: g379 hour tick, g384 win jump at 6, g282-g286 kill after the counter holds at 80. */
static void game_tick(Player *p, uint32_t dt, uint32_t hour_ms) {
    if (p->done || !p->w.in_level) return;
    p->local_hour_ms += dt;
    if (p->local_hour_ms >= hour_ms) { p->local_hour_ms = 0; p->w.hour++; if (p->w.hour > 12) p->w.hour = 1; }
    if (p->freddy >= 80 && !p->kill_in) p->kill_in = 75 * (int)dt;
    if (p->kill_in) { p->kill_in -= (int)dt; if (p->kill_in <= 0) { p->w.gameover = 1; p->freddy = 0; } }
    if (p->w.hour == SYNC_WIN_HOUR && !p->w.gameover) { p->won = true; p->done = true; p->w.in_level = false; p->w.won_screen = true; }
    if (p->w.gameover) { static int dummy; (void)dummy; }
}
static void frame(uint32_t dt, uint32_t hour_ms) {
    now += dt;
    net_pump();
    for (int i = 0; i < PLAYERS; i++) {
        Writes wr; Packet out;
        game_tick(&pl[i], dt, hour_ms);
        sync_step(&pl[i].s, now, &pl[i].w, &wr, &out);
        if (wr.set_hour) pl[i].w.hour = wr.hour;
        if (wr.force_death) { pl[i].freddy = 80; pl[i].deaths_forced++; }
        if (i == 0) for (int c = 1; c < PLAYERS; c++) net_send(0, c, &out);
        else net_send(i, 0, &out);
    }
}
static void run(int frames, uint32_t hour_ms) { for (int i = 0; i < frames; i++) frame(50, hour_ms); }

static void test_win_keeps_everyone_on_host_clock(void) {
    reset(1);
    pl[2].local_hour_ms = 20000; /* this client's own timer drifted */
    run(200, 30000);
    for (int c = 1; c < PLAYERS; c++) CHECK(pl[c].w.hour == pl[0].w.hour, "client %d hour %d host %d", c, pl[c].w.hour, pl[0].w.hour);
    run(4000, 30000);
    for (int i = 0; i < PLAYERS; i++) CHECK(pl[i].won, "player %d did not win", i);
}
static void test_client_death_kills_group(int victim) {
    reset(2);
    run(60, 60000);
    pl[victim].w.gameover = 1;
    run(400, 60000);
    for (int i = 0; i < PLAYERS; i++) CHECK(pl[i].w.gameover == 1, "victim %d: player %d alive", victim, i);
    for (int i = 0; i < PLAYERS; i++) CHECK(!pl[i].won, "player %d won after a death", i);
}
static void test_host_exit_after_win_reaches_late_client(void) {
    reset(1);
    run(40, 60000);
    /* Host jumped from hour 12 before a tick saw 6, and the win frame took a moment to load. */
    pl[0].w.in_level = false;
    run(8, 60000);
    pl[0].w.won_screen = true;
    run(120, 60000);
    for (int c = 1; c < PLAYERS; c++) CHECK(pl[c].won, "client %d missed the win", c);
}
static void test_host_quit_to_menu_does_not_win_or_kill(void) {
    reset(1);
    run(40, 60000);
    pl[0].w.hour = 5; /* late in the night, still not a win */
    run(10, 60000);
    pl[0].w.in_level = false;
    run(200, 60000);
    for (int c = 1; c < PLAYERS; c++) CHECK(!pl[c].won && !pl[c].w.gameover, "client %d affected by host quitting", c);
    CHECK(pl[0].s.result == RESULT_ABORTED, "host quit result %d", pl[0].s.result);
}
static void test_new_night_clears_old_death(void) {
    reset(1);
    run(60, 60000);
    pl[1].w.gameover = 1;
    run(300, 60000);
    for (int i = 0; i < PLAYERS; i++) { pl[i].w = (World){false, 1, 12, 0, false}; pl[i].freddy = pl[i].kill_in = 0; pl[i].deaths_forced = 0; }
    run(40, 60000);
    /* A client can load the next night before the host: the old result must not kill it. */
    pl[3].w.in_level = true;
    run(100, 60000);
    CHECK(!pl[3].w.gameover && pl[3].deaths_forced == 0, "late start killed by old night");
    for (int i = 0; i < 3; i++) pl[i].w.in_level = true;
    run(200, 60000);
    for (int i = 0; i < PLAYERS; i++) CHECK(!pl[i].w.gameover, "player %d died in the new night", i);
    CHECK(pl[0].s.result == RESULT_NONE, "host result not cleared");
}
static void test_night_mismatch_leaves_clock_alone(void) {
    reset(1);
    pl[1].w.night = 2;
    pl[0].w.hour = 3;
    run(40, 600000);
    CHECK(pl[1].w.hour == 12, "mismatched client took host hour %d", pl[1].w.hour);
    CHECK(pl[1].s.night_mismatch, "mismatch not flagged");
}
static void test_lost_host_stops_writes(void) {
    reset(1);
    run(60, 600000);
    uint32_t before = pl[1].w.hour;
    pl[1].w.hour = 2;
    for (int i = 0; i < SYNC_PEER_TIMEOUT_MS / 50 + 20; i++) { now += 50; Writes wr; Packet o; if (i > SYNC_PEER_TIMEOUT_MS / 50 + 2) { sync_step(&pl[1].s, now, &pl[1].w, &wr, &o); CHECK(!wr.set_hour && !wr.force_death, "wrote with no host"); } }
    (void)before;
}
static void test_bad_packets(void) {
    Sync s; Packet p; memset(&p, 0, sizeof p);
    sync_init(&s, false, 1);
    p.magic = 1; p.version = SYNC_VERSION; p.is_host = 1;
    CHECK(!sync_recv(&s, 0, &p, 1), "bad magic accepted");
    p.magic = SYNC_MAGIC; p.version = 99;
    CHECK(!sync_recv(&s, 0, &p, 1), "bad version accepted");
    p.version = SYNC_VERSION; p.result = 200;
    CHECK(!sync_recv(&s, 0, &p, 1), "bad result accepted");
    p.result = 0; p.is_host = 0;
    CHECK(!sync_recv(&s, 0, &p, 1), "client packet accepted by client");
    sync_init(&s, true, 2);
    p.is_host = 1;
    CHECK(!sync_recv(&s, 0, &p, 1), "host packet accepted by host");
    p.is_host = 0;
    CHECK(!sync_recv(&s, 99, &p, 1), "bad peer slot accepted");
    CHECK(sizeof(Packet) == 24, "packet size %zu", sizeof(Packet));
}
int main(void) {
    int nets[][2] = {{0, 0}, {10, 40}, {30, 200}, {50, 400}};
    for (size_t n = 0; n < sizeof nets / sizeof nets[0]; n++) {
        loss_pct = nets[n][0]; jitter_ms = nets[n][1];
        for (int seed = 0; seed < 25; seed++) {
            srand((unsigned)seed * 7919 + (unsigned)n);
            test_win_keeps_everyone_on_host_clock();
            for (int v = 0; v < PLAYERS; v++) test_client_death_kills_group(v);
            test_host_exit_after_win_reaches_late_client();
            test_host_quit_to_menu_does_not_win_or_kill();
            test_new_night_clears_old_death();
            test_night_mismatch_leaves_clock_alone();
            test_lost_host_stops_writes();
        }
    }
    test_bad_packets();
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
