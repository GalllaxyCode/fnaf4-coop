/* Fake-world tests for sync.c: one host and several clients over a lossy, reordering network. */
#include "sync.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PLAYERS 4
#define QUEUE 8192
#define LEVEL 3
#define TITLE 1
#define OVER 4
typedef struct { int to, from; uint32_t at; Packet p; bool used; } Msg;
typedef struct {
    Sync s; Local l; Output o;
    int frame; int place; int kill_seen; int kills_applied; int frame_jumps; int threat; int event_hits[EV_COUNT];
} Actor;
static Actor a[PLAYERS];
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
            sync_recv(&a[q[i].to].s, q[i].from - 1, &q[i].p, now);
            q[i].used = false;
        }
}
static void reset(int frame) {
    memset(q, 0, sizeof q);
    for (int i = 0; i < PLAYERS; i++) {
        char name[16];
        snprintf(name, sizeof name, "p%d", i);
        memset(&a[i], 0, sizeof a[i]);
        sync_init(&a[i].s, i == 0, 1000 + (uint32_t)i + now, name);
        a[i].frame = frame;
    }
}
static void step(uint32_t dt) {
    now += dt;
    net_pump();
    for (int i = 0; i < PLAYERS; i++) {
        Actor *p = &a[i];
        Packet out;
        p->l.frame = p->frame;
        p->l.in_level = p->frame == LEVEL;
        p->l.station = p->place ? p->place : STATION_CENTER;
        p->l.value_count = p->frame == LEVEL ? 2 : 0;
        if (i == 0 && p->frame == LEVEL) {
            p->threat += 3;
            p->l.values[0] = p->threat;
            p->l.values[1] = 7;
        }
        sync_step(&p->s, now, &p->l, &p->o, &out);
        memset(p->l.events, 0, sizeof p->l.events);
        if (p->o.go_to_frame) { p->frame = p->o.frame; p->frame_jumps++; }
        if (p->o.apply_values) p->threat = p->o.values[0];
        if (p->o.apply_kill && !p->l.kill) { p->l.kill = p->o.apply_kill; p->kills_applied++; }
        for (int e = 0; e < EV_COUNT; e++) if (p->o.events[e]) p->event_hits[e]++;
        if (i == 0) for (int c = 1; c < PLAYERS; c++) net_send(0, c, &out);
        else net_send(i, 0, &out);
    }
}
static void run(int n) { for (int i = 0; i < n; i++) step(50); }

static void test_clients_follow_host_to_any_frame(void) {
    reset(TITLE);
    run(100);
    a[0].frame = LEVEL;
    run(300);
    for (int c = 1; c < PLAYERS; c++) CHECK(a[c].frame == LEVEL, "client %d frame %d", c, a[c].frame);
    a[0].frame = 7;
    run(300);
    for (int c = 1; c < PLAYERS; c++) CHECK(a[c].frame == 7, "client %d did not follow to the minigame", c);
}
static void test_threats_mirror_while_in_the_same_frame(void) {
    reset(LEVEL);
    run(400);
    for (int c = 1; c < PLAYERS; c++) {
        int d = a[0].threat - a[c].threat;
        CHECK(a[c].threat > 0 && d >= 0 && d < 3 * 60, "client %d threat %d host %d", c, a[c].threat, a[0].threat);
    }
}
static void test_anyones_door_counts_for_everyone(void) {
    reset(LEVEL);
    run(200);
    a[2].l.input = IN_HOLD_LEFT;
    a[0].l.input = IN_HOLD_RIGHT;
    run(200);
    CHECK(a[0].o.others & IN_HOLD_LEFT, "host missed the client's door");
    CHECK(a[1].o.others & IN_HOLD_LEFT, "client 1 missed client 2's door");
    CHECK(a[1].o.others & IN_HOLD_RIGHT, "client 1 missed the host's door");
    CHECK(a[2].o.others & IN_HOLD_RIGHT, "client 2 missed the host's door");
    CHECK(!(a[2].o.others & IN_HOLD_LEFT), "client 2 sees its own hold as another player's");
    a[2].l.input = 0;
    run(200);
    CHECK(!(a[0].o.others & IN_HOLD_LEFT), "door stayed shut after release");
}
static void test_roster_lists_the_other_players(void) {
    reset(LEVEL);
    a[3].l.station = STATION_BED;
    run(300);
    CHECK(a[0].o.player_count == PLAYERS - 1, "host roster %d", a[0].o.player_count);
    CHECK(a[1].o.player_count == PLAYERS - 1, "client roster %d", a[1].o.player_count);
    for (int i = 0; i < a[1].o.player_count; i++) CHECK(strcmp(a[1].o.players[i].name, "p1") != 0, "client listed itself");
}
static void test_a_death_reaches_everyone_once(int victim, int kind) {
    reset(LEVEL);
    run(200);
    a[victim].l.kill = kind;
    run(300);
    for (int i = 0; i < PLAYERS; i++) {
        CHECK(a[i].l.kill == kind, "victim %d: player %d kill %d", victim, i, a[i].l.kill);
        CHECK(a[i].kills_applied == (i == victim ? 0 : 1), "player %d scared %d times", i, a[i].kills_applied);
    }
}
static void test_retry_clears_the_old_death(void) {
    reset(LEVEL);
    run(200);
    a[1].l.kill = KILL_CHICA;
    run(200);
    for (int i = 0; i < PLAYERS; i++) { a[i].frame = OVER; a[i].l.kill = KILL_NONE; }
    run(100);
    a[3].frame = LEVEL; /* a client can load first, the old kill must not touch it */
    run(40);
    CHECK(a[3].l.kill == KILL_NONE, "late start killed by the previous attempt");
    a[0].frame = LEVEL;
    run(300);
    for (int i = 0; i < PLAYERS; i++) {
        CHECK(a[i].frame == LEVEL, "player %d not back in the night", i);
        CHECK(a[i].l.kill == KILL_NONE, "player %d died in the new attempt", i);
    }
}
static void test_client_scare_finishes_before_it_is_moved(void) {
    reset(LEVEL);
    run(200);
    a[1].l.kill = KILL_BONNIE;
    run(100);
    a[0].frame = OVER; /* host's scare ended first */
    run(20);
    CHECK(a[2].frame == LEVEL, "client moved during its scare");
    run(200);
    CHECK(a[2].frame == OVER, "client never followed after the scare");
}
static void test_events_apply_exactly_once(void) {
    reset(LEVEL);
    run(200);
    for (int i = 0; i < 5; i++) { a[1].l.events[EV_SCARE_BONNIE] = true; run(60); }
    run(200);
    CHECK(a[0].event_hits[EV_SCARE_BONNIE] >= 1 && a[0].event_hits[EV_SCARE_BONNIE] <= 5, "applied %d times", a[0].event_hits[EV_SCARE_BONNIE]);
    if (loss_pct == 0) CHECK(a[0].event_hits[EV_SCARE_BONNIE] == 5, "lost events on a clean network: %d", a[0].event_hits[EV_SCARE_BONNIE]);
}
static void test_lost_host_stops_writes(void) {
    reset(LEVEL);
    run(200);
    Local l = a[1].l;
    now += SYNC_PEER_TIMEOUT_MS + 100;
    Output o; Packet p;
    sync_step(&a[1].s, now, &l, &o, &p);
    CHECK(!o.host_lost && !o.apply_values && !o.go_to_frame && !o.apply_kill, "wrote with no host, or left during a load");
    now += SYNC_HOST_LOST_MS;
    sync_step(&a[1].s, now, &l, &o, &p);
    CHECK(o.host_lost, "host never counted as lost");
    memset(q, 0, sizeof q);
}
static void test_leaving_is_noticed_at_once(void) {
    Sync host, client;
    Local l;
    Output o;
    Packet from_host, from_client;
    memset(&l, 0, sizeof l);
    sync_init(&host, true, 1, "h");
    sync_init(&client, false, 2, "c");
    sync_step(&client, 10, &l, &o, &from_client);
    sync_recv(&host, 0, &from_client, 10);
    sync_step(&host, 20, &l, &o, &from_host);
    CHECK(o.player_count == 1, "client not in the roster");
    sync_recv(&client, 0, &from_host, 20);
    sync_step(&client, 30, &l, &o, &from_client);
    CHECK(!o.host_lost, "connected client counted the host as lost");
    from_client.closing = 1;
    from_client.seq++;
    sync_recv(&host, 0, &from_client, 40);
    sync_step(&host, 50, &l, &o, &from_host);
    CHECK(o.player_count == 0, "leaving client stayed in the roster");
    from_host.closing = 1;
    sync_recv(&client, 0, &from_host, 60);
    sync_step(&client, 70, &l, &o, &from_client);
    CHECK(o.host_lost, "closed room not noticed");
}
/* Modes: `settings` applies to the host, each case runs over the lossy network. */
static void settings(int mode, bool last_standing) {
    a[0].l.mode = mode;
    a[0].l.last_standing = last_standing;
}
static int state_of(int player) {
    if (player == 0) return a[0].o.my_state;
    return a[player].o.my_state;
}
static void test_settings_reach_clients(void) {
    reset(LEVEL);
    settings(MODE_VERSUS, true);
    a[2].l.side = SIDE_FOXY;
    run(200);
    CHECK(a[1].o.mode == MODE_VERSUS && a[1].o.last_standing, "client mode %d", a[1].o.mode);
    bool seen = false;
    for (int i = 0; i < a[1].o.player_count; i++)
        if (strcmp(a[1].o.players[i].name, "p2") == 0) seen = a[1].o.players[i].side == SIDE_FOXY;
    CHECK(seen, "client missed another player's side");
}
static void test_last_standing_keeps_the_night_going(void) {
    reset(LEVEL);
    settings(MODE_DEFAULT, true);
    run(200);
    a[1].l.kill = KILL_BONNIE;
    run(200);
    CHECK(state_of(1) == PLAYER_OUT, "victim state %d", state_of(1));
    CHECK(a[1].l.kill == KILL_BONNIE, "victim lost its kill");
    for (int i = 0; i < PLAYERS; i++) if (i != 1) CHECK(a[i].l.kill == KILL_NONE, "player %d got the scare", i);
    CHECK(a[0].o.survivors == PLAYERS - 1, "survivors %d", a[0].o.survivors);
    a[1].l.kill = KILL_NONE; /* it reloaded */
    a[0].l.kill = KILL_FOXY;
    a[2].l.kill = KILL_FOXY;
    run(200);
    CHECK(!a[0].o.team_lost, "lost with one player left");
    a[3].l.kill = KILL_CHICA;
    run(200);
    CHECK(a[0].o.team_lost, "last death did not end the night");
}
static void test_rescue_saves_the_victim(void) {
    reset(LEVEL);
    settings(MODE_EXTENDED, false);
    a[1].place = STATION_LEFT;
    run(200);
    a[1].l.kill = KILL_BONNIE;
    a[1].place = STATION_NONE;
    run(40);
    CHECK(a[2].o.rescue && a[2].o.rescue_station == STATION_LEFT, "rescue %d at %d", a[2].o.rescue, a[2].o.rescue_station);
    CHECK(state_of(1) == PLAYER_DOWN, "victim state %d", state_of(1));
    a[1].l.kill = KILL_NONE;
    a[2].place = STATION_LEFT;
    a[2].l.input = IN_VIEW_LEFT;
    run(100);
    a[2].l.input = 0;
    run(60);
    CHECK(!a[0].o.rescue, "rescue never ended");
    CHECK(state_of(1) == PLAYER_ALIVE, "victim not back: %d", state_of(1));
    CHECK(a[3].l.kill == KILL_NONE, "scare after a rescue");
    a[3].l.kill = KILL_CHICA; /* the revive is spent */
    run(300);
    for (int i = 0; i < PLAYERS; i++) CHECK(a[i].l.kill != KILL_NONE, "second death spared player %d", i);
}
static void test_missed_rescue_kills_everyone(void) {
    reset(LEVEL);
    settings(MODE_EXTENDED, false);
    run(200);
    a[1].l.kill = KILL_CUPCAKE;
    run(60);
    a[1].l.kill = KILL_NONE;
    run(SYNC_RESCUE_MS / 50 + 120);
    for (int i = 0; i < PLAYERS; i++) CHECK(a[i].l.kill == KILL_CUPCAKE, "player %d kill %d", i, a[i].l.kill);
}
static void test_a_light_elsewhere_does_not_rescue(void) {
    reset(LEVEL);
    settings(MODE_EXTENDED, true);
    a[1].place = STATION_LEFT;
    run(200);
    a[1].l.kill = KILL_BONNIE;
    a[1].place = STATION_NONE;
    a[2].place = STATION_RIGHT;
    a[2].l.input = IN_VIEW_RIGHT;
    run(SYNC_RESCUE_MS / 50 + 120);
    CHECK(state_of(1) == PLAYER_OUT, "wrong spot rescued, state %d", state_of(1));
}
static void test_battery_runs_down_and_recovers(void) {
    reset(LEVEL);
    settings(MODE_EXTENDED, false);
    run(20);
    CHECK(a[0].o.battery == SYNC_BATTERY_FULL, "battery starts at %d", a[0].o.battery);
    a[0].l.input = IN_VIEW_LEFT;
    a[1].l.input = IN_VIEW_RIGHT;
    run(20 * 30);
    CHECK(a[0].o.battery == 0, "two lights for 30 s left %d", a[0].o.battery);
    a[0].l.input = a[1].l.input = 0;
    run(20 * 10);
    CHECK(a[0].o.battery >= 45 && a[0].o.battery <= 55, "recharge gave %d", a[0].o.battery);
    CHECK(a[2].o.battery > 0, "client battery %d", a[2].o.battery);
}
static void test_animatronics_never_die_or_count(void) {
    reset(LEVEL);
    settings(MODE_VERSUS, true);
    a[1].l.side = SIDE_BONNIE;
    run(200);
    CHECK(a[0].o.survivors == PLAYERS - 1, "survivors %d", a[0].o.survivors);
    a[1].l.kill = KILL_FOXY;
    run(100);
    CHECK(state_of(1) == PLAYER_ALIVE, "animatronic died");
    a[1].l.kill = KILL_NONE;
    a[0].l.kill = a[2].l.kill = a[3].l.kill = KILL_FOXY;
    run(200);
    CHECK(a[0].o.team_lost, "all survivors out did not end the night");
}
static void test_last_death_scares_everyone_before_the_night_ends(void) {
    reset(LEVEL);
    settings(MODE_VERSUS, true);
    run(100);
    a[0].l.side = a[2].l.side = a[3].l.side = SIDE_BONNIE;
    run(100);
    a[1].l.kill = KILL_BONNIE;
    run(20);
    CHECK(a[0].o.team_lost, "the last death did not lose the night");
    CHECK(!a[0].o.end_night, "the night ended over the scare");
    CHECK(a[0].kills_applied > 0, "the host's animatronic got no scare");
    CHECK(a[2].kills_applied > 0, "a client's animatronic got no scare");
    CHECK(a[1].frame == LEVEL, "the victim was moved during its scare");
    run(400);
    CHECK(a[0].o.end_night, "a scare that never finished held the night open");
}
static void test_a_leaver_ending_the_night_needs_no_scare(void) {
    reset(LEVEL);
    settings(MODE_VERSUS, true);
    a[1].l.side = a[2].l.side = a[3].l.side = SIDE_FOXY;
    run(200);
    a[0].l.side = SIDE_BONNIE;
    run(20);
    CHECK(a[0].o.team_lost && a[0].o.end_night, "no survivors left, night still running");
}
static void test_a_ghost_keeps_its_scare_when_the_host_moves_on(void) {
    reset(LEVEL);
    settings(MODE_DEFAULT, true);
    run(200);
    a[1].l.scaring = true; /* out already, so it reports no kill */
    a[0].frame = OVER;
    run(20);
    CHECK(a[1].frame == LEVEL, "moved during a scare");
    a[1].l.scaring = false;
    run(20);
    CHECK(a[1].frame == OVER, "never followed after the scare");
}
static void test_commands_arrive_once(void) {
    reset(LEVEL);
    settings(MODE_VERSUS, false);
    a[1].l.side = SIDE_CHICA;
    run(200);
    int moves = 0, zone = -1;
    a[1].l.command_seq = 1;
    a[1].l.command_zone = 5;
    for (int i = 0; i < 200; i++) {
        step(50);
        for (int c = 0; c < a[0].o.command_count; c++) {
            moves++;
            zone = a[0].o.commands[c].zone;
            CHECK(a[0].o.commands[c].side == SIDE_CHICA, "command side %d", a[0].o.commands[c].side);
        }
    }
    CHECK(moves == 1 && zone == 5, "moves %d zone %d", moves, zone);
}
static void test_bad_packets(void) {
    Sync s; Packet p; memset(&p, 0, sizeof p);
    sync_init(&s, false, 1, "x");
    p.magic = 1; p.version = SYNC_VERSION; p.is_host = 1;
    CHECK(!sync_recv(&s, 0, &p, 1), "bad magic accepted");
    p.magic = SYNC_MAGIC; p.version = 99;
    CHECK(!sync_recv(&s, 0, &p, 1), "bad version accepted");
    p.version = SYNC_VERSION; p.kill = 200;
    CHECK(!sync_recv(&s, 0, &p, 1), "bad kill accepted");
    p.kill = 0; p.value_count = 255;
    CHECK(!sync_recv(&s, 0, &p, 1), "bad value count accepted");
    p.value_count = 0; p.player_count = 99;
    CHECK(!sync_recv(&s, 0, &p, 1), "bad player count accepted");
    p.player_count = 0; p.is_host = 0;
    CHECK(!sync_recv(&s, 0, &p, 1), "client packet accepted by client");
    sync_init(&s, true, 2, "x");
    p.is_host = 1;
    CHECK(!sync_recv(&s, 0, &p, 1), "host packet accepted by host");
    p.is_host = 0;
    CHECK(!sync_recv(&s, 99, &p, 1), "bad peer slot accepted");
}
int main(void) {
    int nets[][2] = {{0, 0}, {10, 40}, {30, 200}, {50, 400}};
    for (size_t n = 0; n < sizeof nets / sizeof nets[0]; n++) {
        loss_pct = nets[n][0]; jitter_ms = nets[n][1];
        for (int seed = 0; seed < 10; seed++) {
            srand((unsigned)seed * 7919 + (unsigned)n);
            test_clients_follow_host_to_any_frame();
            test_threats_mirror_while_in_the_same_frame();
            test_anyones_door_counts_for_everyone();
            test_roster_lists_the_other_players();
            for (int v = 0; v < PLAYERS; v++) test_a_death_reaches_everyone_once(v, KILL_CUPCAKE);
            test_retry_clears_the_old_death();
            test_client_scare_finishes_before_it_is_moved();
            test_events_apply_exactly_once();
            test_lost_host_stops_writes();
            test_settings_reach_clients();
            test_last_standing_keeps_the_night_going();
            test_last_death_scares_everyone_before_the_night_ends();
            test_a_leaver_ending_the_night_needs_no_scare();
            test_a_ghost_keeps_its_scare_when_the_host_moves_on();
            test_rescue_saves_the_victim();
            test_missed_rescue_kills_everyone();
            test_a_light_elsewhere_does_not_rescue();
            test_battery_runs_down_and_recovers();
            test_animatronics_never_die_or_count();
            test_commands_arrive_once();
        }
    }
    test_bad_packets();
    test_leaving_is_noticed_at_once();
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
