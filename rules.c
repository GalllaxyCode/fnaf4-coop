#include "rules.h"
#include "game.h"
#include "input.h"
#include "menu.h"
#include "overlay.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define VK_SHIFT_KEY 16
#define VK_CTRL_KEY 17
#define FREDDY_KILL 80
#define BATTERY_READY 100
#define GRIP_FULL 1000
#define GRIP_DRAIN_MS 8000 /* time to tire out, alone */
#define GRIP_RECOVER_MS 6000
#define GRIP_READY 300
#define MOVE_COOLDOWN_MS 4000
#define RESUME_TICKS 8
#define FRAME_GAME_OVER 4
#define FRAME_NIGHTMARE 15
#define WALK_AWAY_TOP 682 /* the `run back` zone, in view coordinates */
#define WALK_AWAY_BOTTOM 762
#define RULE_NIGHTMARE_EXIT 431 /* a scare with `shadow` on leaves the night at once */
#define SCARE_REPORT_MS 250 /* long enough for the death to reach the host first */
#define NIGHTMARE_SCARE_MS 4500 /* the frame's own timer sends it to Extras at 5 s */
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

/* follow AV0 values, from the level sheet. */
enum {
    AV0_LEFT_PEEK = 10,
    AV0_RIGHT_PEEK = 17,
    AV0_LEFT_CLOSING = 20,
    AV0_LEFT_HOLD = 21,
    AV0_RIGHT_CLOSING = 23,
    AV0_RIGHT_HOLD = 24,
    AV0_CLOSET_PEEK = 29,
    AV0_CLOSET_CLOSING = 32,
    AV0_CLOSET_HOLD = 33,
    AV0_FOXY_LEFT = 35,
    AV0_FOXY_RIGHT = 36,
    AV0_BONNIE_HIDE = 40,
    AV0_CHICA_HIDE = 41,
    AV0_BED_VIEW = 43
};

/* Rules that only the host may run, because they change shared threats. The rest stay on everywhere:
 * they read the mirrored threats to draw the player's own view, or they are local ambience. */
static const short client_off_level[] = {
    110, 111, 118, 134, 137, 149, 150, 151, 152, 196, 197, 198, 199, 226, 227, 228, 230, 290, 291, 292, 293, 294, 295, 296,
    297, 299, 302, 308, 320, 321, 322, 323, 324, 325, 326, 327, 328, 329, 345, 346, 348, 365, 366, 367, 368, 379, 380, 381,
    386, 387, 388, 389, 390, 391, 392, 393, 398, 400, 401, 402, 403, 404, 405, 406, 407, 408, 432, 436, 437, 446, 447, 448, 449,
    451, 452, 453, 454, 455, 469, 470, 476, 477, 478};
/* Rules the host needs off because the mod merges these values from every player. */
static const short host_off_level[] = {48, 126, 192, 193, 194, 195};
#define RULE_TRAP_CAUGHT 20 /* the light sends him back to the chair */
static const short client_off_minigame[] = {3, 6, 8, 9, RULE_TRAP_CAUGHT, 44};

/* Fredbear's own roaming and teleports, which a player takes over. */
static const short fredbear_roam_level[] = {151, 152, 326, 327, 328, 329, 446, 447, 448, 449, 478};
/* With `shadow` on these turn the night into Night 7 or 8. */
static const short shadow_night_level[] = {405, 406, 407, 408};
/* His timer at a hall runs on the host alone, and a host playing an animatronic can't die to it. */
static const short hall_timer_level[] = {451, 452, 453, 454, 455};

/* Host reads of `listening mode` that decide threats move to their own counters, one per side, so a
 * player listening at each door both count and the host's breathing audio stays its own. */
static const struct { short group, cond; const wchar_t *counter; } listen_reads[] = {
    {149, 2, L"test 2"}, {153, 0, L"test 2"}, {160, 3, L"test 2"}, {328, 1, L"test 2"}, {329, 1, L"test 2"},
    {150, 2, L"test 3"}, {155, 0, L"test 3"}, {166, 3, L"test 3"}, {326, 1, L"test 3"}, {327, 1, L"test 3"}};

/* ROW_VIEW_X is a position in the night's map layer. That layer scrolls with each player's own view, so it
 * travels as a distance from a landmark and each copy places it for its own view. */
typedef enum { ROW_COUNTER, ROW_X, ROW_Y, ROW_AV, ROW_VIEW_X } RowKind;
typedef struct { const wchar_t *object; RowKind kind; int index; } Row;

static const Row night_rows[] = {
    {L"hour", ROW_COUNTER, 0}, {L"Night", ROW_COUNTER, 0}, {L"shadow", ROW_COUNTER, 0}, {L"Bonnie AI", ROW_COUNTER, 0},
    {L"Chica AI", ROW_COUNTER, 0}, {L"Foxy AI", ROW_COUNTER, 0}, {L"Freddy AI", ROW_COUNTER, 0}, {L"Fredbear AI", ROW_COUNTER, 0},
    {L"Freddy counter", ROW_COUNTER, 0}, {L"Bonnie", ROW_VIEW_X, 0}, {L"Bonnie", ROW_Y, 0}, {L"Chica", ROW_VIEW_X, 0}, {L"Chica", ROW_Y, 0},
    {L"foxy", ROW_VIEW_X, 0}, {L"foxy", ROW_Y, 0}, {L"Fredbear", ROW_VIEW_X, 0}, {L"Fredbear", ROW_Y, 0}, {L"foxy", ROW_AV, 2},
    {L"Bonnie", ROW_AV, 6}, {L"Chica", ROW_AV, 6}, {L"Fredbear", ROW_AV, 0}, {L"Fredbear", ROW_AV, 6}, {L"Fredbear", ROW_AV, 7}};
enum { V_HOUR, V_NIGHT, V_SHADOW, V_BONNIE_AI, V_CHICA_AI, V_FOXY_AI, V_FREDDY_AI, V_FREDBEAR_AI, V_FREDDY, V_BONNIE_X, V_BONNIE_Y, V_CHICA_X, V_CHICA_Y, V_FOXY_X, V_FOXY_Y, V_FRED_X, V_FRED_Y };

static const Row mini_rows[] = {{L"Counter", ROW_COUNTER, 0}, {L"becoming active", ROW_COUNTER, 0}, {L"random", ROW_COUNTER, 0}, {L"is aggressive?", ROW_COUNTER, 0}, {L"plushtrap", ROW_X, 0}, {L"plushtrap", ROW_Y, 0}};
enum { M_COUNTER, M_ACTIVE, M_RANDOM, M_AGGRESSIVE, M_X, M_Y };

/* Plushtrap's path, and the `random` value that sends him each way (0 when there is only one way). */
enum { TRAP_CHAIR, TRAP_HALL_1, TRAP_FAR_LEFT, TRAP_FAR_RIGHT, TRAP_HALL_2, TRAP_CLOSE_LEFT, TRAP_CLOSE_RIGHT, TRAP_HALL_3, TRAP_GOT_YOU, TRAP_ZONES };
static const wchar_t *const trap_zones[TRAP_ZONES] = {L"in chair", L"hall stage 1", L"far left room", L"far right room", L"hall stage 2", L"close left room", L"close right room", L"hall stage 3", L"got you"};
static const signed char trap_steps[][3] = {
    {TRAP_CHAIR, TRAP_HALL_1, 0}, {TRAP_HALL_1, TRAP_FAR_LEFT, 1}, {TRAP_HALL_1, TRAP_FAR_RIGHT, 2}, {TRAP_FAR_LEFT, TRAP_HALL_2, 0}, {TRAP_FAR_RIGHT, TRAP_HALL_2, 0},
    {TRAP_HALL_2, TRAP_CLOSE_LEFT, 1}, {TRAP_HALL_2, TRAP_CLOSE_RIGHT, 2}, {TRAP_CLOSE_LEFT, TRAP_HALL_3, 0}, {TRAP_CLOSE_RIGHT, TRAP_HALL_3, 0}, {TRAP_HALL_3, TRAP_GOT_YOU, 0}};

static const Row night_title_rows[] = {{L"Night", ROW_COUNTER, 0}, {L"shadow", ROW_COUNTER, 0}};

/* A ghost (dead, or playing an animatronic) can't be killed, and its own position counts for nothing. */
static const short ghost_off_level[] = {43, 44, 45, 200, 201, 215, 216, 252, 253, 256, 286, 287, 369, 399};

static bool host;
static int mode, side, my_state;
static bool resume_needed, away;
static int resume_ticks;
static uint32_t away_since, last_goto_ms;
static int32_t saved[SYNC_VALUES];
static int saved_count;
static int grip = GRIP_FULL;
static bool grip_spent;
static uint8_t command_seq, command_zone;
static uint32_t map_cooldown_until;
static uint32_t move_ready_ms[SIDE_COUNT];
static int ai_base[5], ai_written[5];
static int my_kill;
static int forced_kind;
static int last_av0 = -1;
static bool force_active;
static bool scare_on; /* a shared scare is on its way, or playing, and everyone watches it */
static int *patched_literal;
static int patched_literal_old;
static BYTE *patched_for;
static uint32_t last_closet_ms, last_fred_closet_ms, last_fred_bed_ms;
static int side_restore = -1;
static char debug_line[160];

void rules_init(bool is_host) {
    host = is_host;
    my_state = PLAYER_ALIVE;
    resume_needed = away = false;
    resume_ticks = 0;
    grip = GRIP_FULL;
    grip_spent = false;
    command_seq = 0;
    key_block(16, false);
    key_block(17, false);
}

void rules_setup(int new_mode, int new_side) {
    mode = new_mode;
    side = new_side;
}

static bool playing_animatronic(void) { return mode == MODE_VERSUS && side != SIDE_SURVIVOR; }
static bool is_ghost(void) { return my_state != PLAYER_ALIVE || playing_animatronic(); }
bool rules_map_open(void) { return playing_animatronic() && game_frame() == FRAME_LEVEL && !scare_on; }
void rules_command(int zone) {
    command_seq++;
    command_zone = (uint8_t)zone;
}
const char *rules_debug(void) { return debug_line; }
bool rules_host_off_is_active(void) { return evg_is_disabled(host_off_level[1]) || evg_is_disabled(client_off_level[1]); }

static bool row_read(const Row *r, int32_t *out) {
    BYTE *o = obj(r->object);
    int a, b;
    if (!o) return false;
    switch (r->kind) {
    case ROW_COUNTER: return cnt_read(o, (int *)out);
    case ROW_X: if (!pos_read(o, &a, &b)) return false; *out = a; return true;
    case ROW_VIEW_X: {
        int mark, unused;
        if (!pos_read(o, &a, &b) || !pos_read(obj(L"living room center"), &mark, &unused)) return false;
        *out = a - mark;
        return true;
    }
    case ROW_Y: if (!pos_read(o, &a, &b)) return false; *out = b; return true;
    case ROW_AV: return av_read(o, r->index, (int *)out);
    }
    return false;
}

static void row_write(const Row *r, int value) {
    BYTE *o = obj(r->object);
    int x, y;
    if (!o) return;
    switch (r->kind) {
    case ROW_COUNTER: cnt_write(o, value); break;
    case ROW_X: if (pos_read(o, &x, &y)) pos_write(o, value, y); break;
    case ROW_VIEW_X: {
        int mark, unused;
        if (pos_read(o, &x, &y) && pos_read(obj(L"living room center"), &mark, &unused)) pos_write(o, mark + value, y);
        break;
    }
    case ROW_Y: if (pos_read(o, &x, &y)) pos_write(o, x, value); break;
    case ROW_AV: av_write(o, r->index, value); break;
    }
}

static void disable_all(const short *list, size_t n) {
    for (size_t i = 0; i < n; i++) evg_disable(list[i]);
}

static void sides_taken(const Output *o, bool taken[SIDE_COUNT]) {
    memset(taken, 0, SIDE_COUNT * sizeof *taken);
    taken[side] = true;
    for (int i = 0; i < o->player_count; i++) taken[o->players[i].side] = true;
}

/* Rule switches that depend on the player, kept up to date on every tick. */
static void role_rules(const Output *o) {
    if (game_frame() != FRAME_LEVEL) return;
    bool ghost = is_ghost() && !scare_on;
    for (size_t i = 0; i < COUNT(ghost_off_level); i++) {
        if (ghost) evg_disable(ghost_off_level[i]);
        else evg_enable(ghost_off_level[i]);
    }
    if (is_ghost() || !host) evg_disable(400);
    else evg_enable(400);
    if (mode == MODE_COOP && side == SIDE_EYES) {
        evg_disable(190);
        evg_disable(191);
        evg_disable(466);
        evg_disable(467);
    }
    if (!host && mode == MODE_VERSUS)
        for (size_t i = 0; i < COUNT(hall_timer_level); i++) evg_enable(hall_timer_level[i]);
    if (host && mode == MODE_VERSUS) {
        bool taken[SIDE_COUNT];
        sides_taken(o, taken);
        if (taken[SIDE_BONNIE]) { evg_disable(149); evg_disable(436); }
        if (taken[SIDE_CHICA]) { evg_disable(150); evg_disable(437); }
        if (taken[SIDE_FOXY]) evg_disable(118);
        if (taken[SIDE_FREDBEAR] || taken[SIDE_NIGHTMARE]) disable_all(fredbear_roam_level, COUNT(fredbear_roam_level));
        if (taken[SIDE_NIGHTMARE]) disable_all(shadow_night_level, COUNT(shadow_night_level));
    }
}

/* A frame's events are reloaded when it restarts, so the patches are checked on every tick. */
void rules_frame(void) {
    int frame = game_frame();
    bool minigame = frame == FRAME_PLUSHTRAP || frame == FRAME_BB;
    if (frame == FRAME_LEVEL) {
        int marker = host ? 192 : 111;
        if (evg_is_disabled(marker)) return;
        patched_literal = NULL;
        if (host) {
            disable_all(host_off_level, COUNT(host_off_level));
            for (size_t i = 0; i < COUNT(listen_reads); i++)
                cond_retarget(cond_at(evg(listen_reads[i].group), listen_reads[i].cond), listen_reads[i].counter);
        } else {
            disable_all(client_off_level, COUNT(client_off_level));
        }
        patched_for = evg_base();
    } else if (minigame && !host) {
        if (evg_is_disabled(9)) return;
        disable_all(client_off_minigame, COUNT(client_off_minigame));
    }
}

static int station_of(int av0) {
    if (av0 <= 6) return STATION_CENTER;
    if (av0 == AV0_CHICA_HIDE && obj(L"Chica hide")) return STATION_RIGHT;
    if ((av0 >= 7 && av0 <= 13) || (av0 >= 20 && av0 <= 22) || av0 == AV0_FOXY_LEFT || av0 == AV0_BONNIE_HIDE) return STATION_LEFT;
    if ((av0 >= 14 && av0 <= 19) || (av0 >= 23 && av0 <= 25) || av0 == AV0_FOXY_RIGHT) return STATION_RIGHT;
    if (av0 >= 26 && av0 <= 34) return STATION_CLOSET;
    if (av0 >= 41 && av0 <= 45) return STATION_BED;
    return STATION_CENTER;
}

static int trap_zone(int x, int y) {
    for (int i = 0; i < TRAP_ZONES; i++) {
        int zx, zy;
        if (pos_read(obj(trap_zones[i]), &zx, &zy) && abs(x - zx) <= 2 && abs(y - zy) <= 2) return i;
    }
    return -1;
}

static int trap_roll(int from, int to) {
    for (size_t i = 0; i < COUNT(trap_steps); i++)
        if (trap_steps[i][0] == from && trap_steps[i][1] == to) return trap_steps[i][2];
    return -1;
}

/* The game's rules take each step, for the footstep and the catch animation, or take over from a
 * written position and walk him one stage past the host. A step that didn't land by the next tick
 * is written, as is anything the rules can't reach in one step. */
static void mirror_trap(const int32_t *v) {
    static bool stepping;
    static int steps, writes;
    BYTE *trap = obj(L"plushtrap");
    int x, y;
    evg_disable(RULE_TRAP_CAUGHT);
    if (!pos_read(trap, &x, &y)) return;
    snprintf(debug_line, sizeof debug_line, "game=%d,%d host=%d,%d steps=%d writes=%d", x, y, v[M_X], v[M_Y], steps, writes);
    if (x == v[M_X] && y == v[M_Y]) {
        stepping = false;
        return;
    }
    if (!stepping) {
        int from = trap_zone(x, y), to = trap_zone(v[M_X], v[M_Y]);
        if (from < 0) from = TRAP_CHAIR; /* he starts inside the chair's zone, off its exact spot */
        int roll = trap_roll(from, to);
        if (roll >= 0) {
            if (roll > 0) cnt_set(L"random", roll);
            cnt_set(L"move", 1);
            stepping = true;
            steps++;
            return;
        }
        if (from == TRAP_HALL_1 && to == TRAP_CHAIR) {
            evg_enable(RULE_TRAP_CAUGHT);
            stepping = true;
            steps++;
            return;
        }
    }
    stepping = false;
    writes++;
    cnt_set(L"move", 0);
    pos_write(trap, v[M_X], v[M_Y]);
}

static int kill_kind(void) {
    if (obj(L"cupcake attack")) return KILL_CUPCAKE;
    if (obj(L"bonnie bedroom attack")) return KILL_BONNIE_BED;
    if (obj(L"freddy bedroom attack")) return KILL_FREDDY_BED;
    if (obj(L"fredbear scare")) return KILL_FREDBEAR;
    int got = cnt_get(L"got you", 0);
    if (got == 1) return KILL_BONNIE;
    if (got == 2) return KILL_CHICA;
    return cnt_get(L"Freddy counter", 0) >= 60 ? KILL_FREDDY : KILL_FOXY;
}

void rules_read(Local *l) {
    memset(l, 0, sizeof *l);
    l->frame = game_frame();
    l->station = STATION_NONE;
    int frame = l->frame;
    bool ctrl = key_usable(VK_CTRL_KEY), shift = key_usable(VK_SHIFT_KEY);
    l->side = side;
    l->command_seq = command_seq;
    l->command_zone = command_zone;
    if (frame == FRAME_PLUSHTRAP || frame == FRAME_BB) {
        if (ctrl) l->input |= IN_LIGHT;
        for (size_t i = 0; i < COUNT(mini_rows); i++)
            if (!row_read(&mini_rows[i], &l->values[i])) return;
        l->value_count = COUNT(mini_rows);
        return;
    }
    if (frame == FRAME_WHAT_NIGHT) {
        for (size_t i = 0; i < COUNT(night_title_rows); i++)
            if (!row_read(&night_title_rows[i], &l->values[i])) return;
        l->value_count = COUNT(night_title_rows);
        return;
    }
    BYTE *follow = obj(L"follow");
    int av0;
    if (frame != FRAME_LEVEL || !follow || !av_read(follow, 0, &av0) || !obj(L"gameover") || !obj(L"hour")) {
        my_kill = KILL_NONE;
        forced_kind = KILL_NONE;
        last_av0 = -1;
        force_active = false;
        scare_on = false;
        l->scaring = frame == FRAME_NIGHTMARE;
        return;
    }
    l->in_level = true;
    l->scaring = cnt_get(L"gameover", 0) > 0;
    l->station = is_ghost() ? STATION_NONE : station_of(av0);
    if (is_ghost()) {
        for (size_t i = 0; i < COUNT(night_rows); i++)
            if (!row_read(&night_rows[i], &l->values[i])) return;
        l->value_count = COUNT(night_rows);
        /* A reload reads a fresh night here, which must not replace the one being restored. */
        if (!away && resume_ticks == 0) {
            memcpy(saved, l->values, sizeof saved);
            saved_count = l->value_count;
        }
        my_kill = KILL_NONE;
        return;
    }
    if (av0 == AV0_LEFT_HOLD && shift) l->input |= IN_HOLD_LEFT;
    if (av0 == AV0_RIGHT_HOLD && shift) l->input |= IN_HOLD_RIGHT;
    if (av0 == AV0_CLOSET_HOLD && shift) l->input |= IN_HOLD_CLOSET;
    if (av0 == AV0_LEFT_PEEK && ctrl) l->input |= IN_VIEW_LEFT;
    if (av0 == AV0_RIGHT_PEEK && ctrl) l->input |= IN_VIEW_RIGHT;
    if (av0 == AV0_CLOSET_PEEK && ctrl) l->input |= IN_VIEW_CLOSET;
    if (av0 == AV0_BED_VIEW && ctrl) l->input |= IN_VIEW_BED;
    if (av0 == AV0_LEFT_PEEK && !ctrl && !shift) l->input |= IN_LISTEN_LEFT;
    if (av0 == AV0_RIGHT_PEEK && !ctrl && !shift) l->input |= IN_LISTEN_RIGHT;
    int side = cnt_get(L"what side", 3);
    if (side == 1) l->input |= IN_SIDE_LEFT;
    if (side == 5) l->input |= IN_SIDE_RIGHT;

    if (av0 != last_av0) {
        if (av0 == AV0_BONNIE_HIDE) l->events[EV_SCARE_BONNIE] = true;
        if (av0 == AV0_CHICA_HIDE && last_av0 == AV0_RIGHT_PEEK) l->events[EV_SCARE_CHICA] = true;
        if (av0 == AV0_FOXY_LEFT) l->events[EV_SCARE_FOXY_LEFT] = true;
        if (av0 == AV0_FOXY_RIGHT) l->events[EV_SCARE_FOXY_RIGHT] = true;
        if (av0 == 8 || av0 == 15 || av0 == 27) l->events[EV_MOVED] = true;
        last_av0 = av0;
    }
    if (cnt_get(L"gameover", 0) > 0 && my_kill == KILL_NONE) my_kill = forced_kind != KILL_NONE ? forced_kind : kill_kind();
    l->kill = my_kill;
    for (size_t i = 0; i < COUNT(night_rows); i++)
        if (!row_read(&night_rows[i], &l->values[i])) return;
    l->value_count = COUNT(night_rows);
    if (!away && resume_ticks == 0) {
        memcpy(saved, l->values, sizeof saved);
        saved_count = l->value_count;
    }
}

bool rules_host_resume(Local *l, uint32_t now) {
    if (!host || !resume_needed) return false;
    if (l->frame == FRAME_LEVEL) {
        if (away) resume_ticks = RESUME_TICKS;
        away = false;
        if (resume_ticks > 0) memcpy(l->values, saved, sizeof l->values);
        return false;
    }
    if (l->frame != FRAME_GAME_OVER && l->frame != FRAME_NIGHTMARE) {
        if (!away) {
            resume_needed = false;
            return false;
        }
    }
    if (!away) {
        away = true;
        away_since = now;
    }
    if (now - away_since > 500 && now - last_goto_ms > 3000) {
        last_goto_ms = now;
        game_goto(FRAME_LEVEL);
    }
    int frame = FRAME_LEVEL;
    int count = saved_count;
    memset(l, 0, sizeof *l);
    l->frame = frame;
    l->in_level = true;
    l->side = side;
    l->value_count = count;
    memcpy(l->values, saved, sizeof l->values);
    return true;
}

/* Runs the same scare the victim saw, through the rule that shows it on a normal death. */
static void inject(int group, const wchar_t *threat, const wchar_t *zone, const wchar_t *viewing, int anim, int create_group, int create_action) {
    bool chica = group == 201;
    BYTE *g = evg(group), *cond = cond_at(g, 1);
    if (!cond) return;
    int size = *(short *)cond;
    if (size < 0) size = -size;
    int *literal = literal_in(cond, size, 1);
    if (literal) {
        patched_literal = literal;
        patched_literal_old = 1;
        *literal = 0;
    }
    BYTE *anim_param = act_param(act_at(g, chica ? 3 : 2), 0);
    if (anim_param) *(short *)(anim_param + 4) = (short)anim;
    if (create_group >= 0) {
        BYTE *to = act_param(act_at(g, chica ? 2 : 1), 0), *from = act_param(act_at(evg(create_group), create_action), 0);
        if (to && from && *(short *)to == *(short *)from) memcpy(to, from, (size_t)*(short *)from);
    }
    BYTE *t = obj(threat);
    int zx, zy;
    if (pos_read(obj(zone), &zx, &zy)) pos_write(t, zx + 1, zy + 1);
    cnt_set(L"got you", 0);
    cnt_set(viewing, 0);
}

void rules_force_kill(int kind) {
    if (cnt_get(L"gameover", 0) > 0) return;
    force_active = true;
    forced_kind = kind;
    switch (kind) {
    case KILL_BONNIE: inject(200, L"Bonnie", L"left hall near", L"viewing left hall", 13, -1, 0); break;
    case KILL_CHICA: inject(201, L"Chica", L"right hall near", L"viewing right hall", 12, -1, 0); break;
    case KILL_FREDDY: inject(200, L"Bonnie", L"left hall near", L"viewing left hall", 14, -1, 0); break;
    case KILL_FOXY: inject(200, L"Bonnie", L"left hall near", L"viewing left hall", 0, -1, 0); break;
    case KILL_CUPCAKE: inject(201, L"Chica", L"right hall near", L"viewing right hall", 12, 215, 1); break;
    case KILL_BONNIE_BED: inject(200, L"Bonnie", L"left hall near", L"viewing left hall", 13, 216, 2); break;
    case KILL_FREDDY_BED:
        cnt_set(L"Freddy counter", FREDDY_KILL);
        av_write(obj(L"black flash"), 3, 2);
        break;
    case KILL_FREDBEAR:
        cnt_set(L"fredcheck", 1);
        av_write(obj(L"Fredbear"), 6, 10);
        break;
    default: break;
    }
}

static void apply_rows(const Row *rows, size_t n, const int32_t *values);
static const wchar_t *const zones[] = {L"living room center", L"living room left", L"living room right", L"left hall far", L"left hall near", L"right hall far", L"right hall near", L"kitchen", L"in closet", L"on bed"};
enum { Z_NONE = -1, Z_CENTER, Z_LIVING_LEFT, Z_LIVING_RIGHT, Z_LEFT_FAR, Z_LEFT_NEAR, Z_RIGHT_FAR, Z_RIGHT_NEAR, Z_KITCHEN, Z_CLOSET, Z_BED };

static int zone_of(int x, int y) {
    for (size_t i = 0; i < COUNT(zones); i++) {
        int zx, zy;
        if (pos_read(obj(zones[i]), &zx, &zy) && abs(x - zx) <= 2 && abs(y - zy) <= 2) return (int)i;
    }
    return Z_NONE;
}

/* Footsteps are the game's main warning, so a mirrored move sets the same flag a real move sets. */
static void fredbear_move_sound(int zone) {
    if (zone == Z_LIVING_LEFT) av_write(obj(L"follow"), 11, 3);
    if (zone == Z_LIVING_RIGHT) av_write(obj(L"follow"), 17, 3);
}

static void play_move_sounds(const int32_t *v) {
    BYTE *follow = obj(L"follow");
    int x, y, mark, unused;
    if (!pos_read(obj(L"living room center"), &mark, &unused)) return;
    if (pos_read(obj(L"Bonnie"), &x, &y) && (x != v[V_BONNIE_X] + mark || y != v[V_BONNIE_Y])) {
        int z = zone_of(v[V_BONNIE_X] + mark, v[V_BONNIE_Y]);
        if (z == Z_LEFT_FAR || z == Z_LEFT_NEAR) av_write(follow, 21, 1 + rand() % 4);
        else if (z == Z_CENTER || z == Z_LIVING_LEFT) av_write(follow, 23, 1 + rand() % 3);
    }
    if (pos_read(obj(L"Chica"), &x, &y) && (x != v[V_CHICA_X] + mark || y != v[V_CHICA_Y])) {
        int z = zone_of(v[V_CHICA_X] + mark, v[V_CHICA_Y]);
        if (z == Z_RIGHT_FAR || z == Z_RIGHT_NEAR) av_write(follow, 22, 1 + rand() % 4);
        else if (z != Z_NONE) av_write(follow, 24, 1 + rand() % 3);
    }
    if (pos_read(obj(L"foxy"), &x, &y) && (x != v[V_FOXY_X] + mark || y != v[V_FOXY_Y])) {
        int z = zone_of(v[V_FOXY_X] + mark, v[V_FOXY_Y]);
        if (z == Z_LIVING_LEFT) av_write(follow, 11, 1 + rand() % 2);
        else if (z == Z_LIVING_RIGHT) av_write(follow, 17, 1 + rand() % 2);
    }
    if (pos_read(obj(L"Fredbear"), &x, &y) && (x != v[V_FRED_X] + mark || y != v[V_FRED_Y])) fredbear_move_sound(zone_of(v[V_FRED_X] + mark, v[V_FRED_Y]));
}

static void apply_rows(const Row *rows, size_t n, const int32_t *values) {
    for (size_t i = 0; i < n; i++) row_write(&rows[i], values[i]);
}

/* Merges every other player's input into the host's simulation. */
static void host_merge(const Output *o, const Local *l, uint32_t now) {
    int av0 = 0;
    bool ctrl = key_usable(VK_CTRL_KEY);
    uint16_t others = o->others;
    av_read(obj(L"follow"), 0, &av0);
    if (is_ghost()) {
        av0 = -1;
        ctrl = false;
    }

    cnt_set(L"left door shut", av0 == AV0_LEFT_HOLD || (others & IN_HOLD_LEFT));
    cnt_set(L"right door shut", av0 == AV0_RIGHT_HOLD || (others & IN_HOLD_RIGHT));
    cnt_set(L"test 2", (l->input & IN_LISTEN_LEFT) || (others & IN_LISTEN_LEFT) ? 1 : 0);
    cnt_set(L"test 3", (l->input & IN_LISTEN_RIGHT) || (others & IN_LISTEN_RIGHT) ? 2 : 0);
    if (!force_active) {
        cnt_set(L"viewing left hall", (others & IN_VIEW_LEFT) || (av0 == AV0_LEFT_PEEK && ctrl));
        cnt_set(L"viewing right hall", (others & IN_VIEW_RIGHT) || (av0 == AV0_RIGHT_PEEK && ctrl));
        cnt_set(L"viewing closet", (others & IN_VIEW_CLOSET) || (av0 == AV0_CLOSET_PEEK && ctrl));
    }

    BYTE *foxy = obj(L"foxy"), *fred = obj(L"Fredbear");
    if (others & IN_HOLD_CLOSET) {
        if (at_zone(foxy, L"in closet") && now - last_closet_ms >= 1000) {
            int stage;
            last_closet_ms = now;
            if (av_read(foxy, 2, &stage)) av_write(foxy, 2, stage > 0 ? stage - 1 : 0);
        }
        if (at_zone(fred, L"in closet") && now - last_fred_closet_ms >= 3000) {
            int dir = 1, zx, zy;
            last_fred_closet_ms = now;
            av_read(fred, 0, &dir);
            if (pos_read(obj(dir == 1 ? L"living room left" : L"living room right"), &zx, &zy)) pos_write(fred, zx, zy);
        }
    }
    if (others & IN_VIEW_BED) {
        int counter = cnt_get(L"Freddy counter", 0);
        cnt_set(L"Freddy counter", counter > 0 ? counter - 1 : 0);
        if (at_zone(fred, L"on bed") && now - last_fred_bed_ms >= 1000) {
            int stare = 0;
            last_fred_bed_ms = now;
            av_read(fred, 7, &stare);
            av_write(fred, 7, stare + 1);
        }
    }

    /* Foxy runs into the closet when the player is on the far side, and any player counts. */
    if (side_restore >= 0) {
        cnt_set(L"what side", side_restore);
        side_restore = -1;
    }
    int side = cnt_get(L"what side", 3);
    if (at_zone(foxy, L"left hall far") && (others & IN_SIDE_RIGHT) && side != 5) {
        side_restore = side;
        cnt_set(L"what side", 5);
    } else if (at_zone(foxy, L"right hall far") && (others & IN_SIDE_LEFT) && side != 1) {
        side_restore = side;
        cnt_set(L"what side", 1);
    }

    int zx, zy;
    if (o->events[EV_SCARE_BONNIE] && pos_read(obj(L"living room center"), &zx, &zy)) {
        pos_write(obj(L"Bonnie"), zx, zy + 1);
        av_write(obj(L"Bonnie"), 1, 0);
    }
    if (o->events[EV_SCARE_CHICA] && pos_read(obj(L"living room center"), &zx, &zy)) {
        pos_write(obj(L"Chica"), zx, zy + 1);
        av_write(obj(L"Chica"), 1, 0);
    }
    if ((o->events[EV_SCARE_FOXY_LEFT] || o->events[EV_SCARE_FOXY_RIGHT]) && pos_read(obj(L"living room center"), &zx, &zy))
        pos_write(foxy, zx, zy + 1);
    if (o->events[EV_MOVED]) {
        av_write(fred, 12, 0);
        av_write(fred, 13, 0);
        av_write(fred, 14, 0);
    }
}

/* The game ignores the walk-away zone while Shift or Ctrl is down, so a friend's key would pin this
 * player in place. A cursor reaching the zone with the turn armed is the player leaving, so the
 * friend's keys let go here, and the game's own rules carry the player away. */
static bool walking_away(void) {
    POINT at;
    int armed = 1, x, y;
    return av_read(obj(L"run back"), 0, &armed) && armed == 0 && cursor_get(&at) && overlay_view_point(at, &x, &y) && y >= WALK_AWAY_TOP && y < WALK_AWAY_BOTTOM;
}

/* A door held by a friend closes on this player's screen through the game's own animation. */
static void shared_keys(const Output *o, const Local *l) {
    int frame = game_frame();
    bool shift = false, ctrl = false;
    if (frame == FRAME_PLUSHTRAP || frame == FRAME_BB) ctrl = o->others & IN_LIGHT;
    if (l->in_level && !walking_away()) {
        int av0 = 0;
        av_read(obj(L"follow"), 0, &av0);
        /* Standing at the same spot as a friend, their light is yours too. */
        if (av0 == AV0_LEFT_PEEK && (o->others & IN_VIEW_LEFT)) ctrl = true;
        if (av0 == AV0_RIGHT_PEEK && (o->others & IN_VIEW_RIGHT)) ctrl = true;
        if (av0 == AV0_CLOSET_PEEK && (o->others & IN_VIEW_CLOSET)) ctrl = true;
        if (av0 == AV0_BED_VIEW && (o->others & IN_VIEW_BED)) ctrl = true;
        if ((av0 == AV0_LEFT_PEEK || av0 == AV0_LEFT_CLOSING || av0 == AV0_LEFT_HOLD) && (o->others & IN_HOLD_LEFT)) shift = true;
        if ((av0 == AV0_RIGHT_PEEK || av0 == AV0_RIGHT_CLOSING || av0 == AV0_RIGHT_HOLD) && (o->others & IN_HOLD_RIGHT)) shift = true;
        if ((av0 == AV0_CLOSET_PEEK || av0 == AV0_CLOSET_CLOSING || av0 == AV0_CLOSET_HOLD) && (o->others & IN_HOLD_CLOSET)) shift = true;
    }
    key_force(VK_SHIFT_KEY, shift);
    key_force(VK_CTRL_KEY, ctrl);
}

/* Door openings as seen from the room after turning, in 1024x768 view coordinates. */
#define DOOR_SWING_MS 300
#define DOOR_COLOR 0x8F95B3
static const OverlayQuad left_door = {{0, 134, 100, 0}, {104, 213, 692, 723}, 0, 255, 90};
static const OverlayQuad right_door = {{894, 1024, 1024, 930}, {213, 104, 723, 692}, 0, 255, 10};

/* The gap between the closet's doors, from the room. */
static const OverlayQuad closet_gap_center = {{575, 612, 612, 581}, {233, 233, 643, 643}, 0, 255, 0, true};
static const OverlayQuad closet_gap_right = {{438, 474, 474, 442}, {232, 232, 652, 652}, 0, 255, 0, true};
#define CLOSET_COLOR_LIT 0xD6D9E4
#define CLOSET_COLOR_DIM 0x565A70

static void add_door(OverlayScene *scene, const OverlayQuad *door, bool left, double open) {
    if (open >= 1) return;
    OverlayQuad q = *door;
    q.color = door->louver ? (door == &closet_gap_center ? CLOSET_COLOR_LIT : CLOSET_COLOR_DIM) : DOOR_COLOR;
    /* The panel swings out from the hinge side as it closes. */
    if (left) {
        q.x[1] = (int)(door->x[1] - (door->x[1] - door->x[0]) * open);
        q.x[2] = (int)(door->x[2] - (door->x[2] - door->x[3]) * open);
    } else {
        q.x[0] = (int)(door->x[0] + (door->x[1] - door->x[0]) * open);
        q.x[3] = (int)(door->x[3] + (door->x[2] - door->x[3]) * open);
    }
    scene->quads[scene->quad_count++] = q;
}

/* Moves a door's openness towards its target, so the panel animates instead of popping in. */
static double swing(double open, bool shut, uint32_t dt) {
    double step = (double)dt / DOOR_SWING_MS;
    open += shut ? -step : step;
    return open < 0 ? 0 : open > 1 ? 1 : open;
}

static void add_bubble(OverlayScene *scene, int station, int view, const Player *p, int *stack) {
    /* Where each station appears from each view: the turned views show one door and the closet. */
    int x = 512, y = 560, slot = station;
    const char *before = "", *after = "";
    bool same_spot = station != STATION_CENTER && station == station_of(view);
    switch (same_spot ? STATION_NONE : station) {
    case STATION_NONE:
        y = 120;
        break;
    case STATION_LEFT:
        if (view == 2) { x = 70; y = 260; }
        else { x = 0; y = 300; before = "< "; }
        break;
    case STATION_RIGHT:
        if (view == 5) { x = 954; y = 260; }
        else { x = 1024; y = 300; after = " >"; }
        break;
    case STATION_CLOSET:
        if (view == 2) { x = 613; y = 220; }
        else if (view == 5) { x = 446; y = 220; }
        else { x = 512; y = 200; }
        break;
    case STATION_BED:
        x = 512;
        y = 716;
        after = " v";
        break;
    default:
        x = view == 2 ? 300 : view == 5 ? 724 : 512;
        y = 560;
        break;
    }
    if (scene->bubble_count >= OVERLAY_MAX_BUBBLES) return;
    OverlayBubble *b = &scene->bubbles[scene->bubble_count++];
    b->x = x;
    b->y = y - stack[slot] * 40;
    stack[slot]++;
    const char *held = p->input & (IN_HOLD_LEFT | IN_HOLD_RIGHT | IN_HOLD_CLOSET) ? " * holding door" : "";
    snprintf(b->text, OVERLAY_TEXT_LEN, "%s%s%s%s", before, p->name, held, after);
}

/* Versus: how each animatronic may move, in the game's own zones. Fredbear and Nightmare appear in
 * the closet or on the bed from a hall, the way the game teleports them. */
#define FREDBEAR_ROUTES {[Z_CENTER] = {Z_LIVING_LEFT, Z_LIVING_RIGHT, -1}, [Z_LIVING_LEFT] = {Z_LEFT_FAR, Z_LIVING_RIGHT, Z_CENTER}, [Z_LIVING_RIGHT] = {Z_RIGHT_FAR, Z_LIVING_LEFT, Z_CENTER}, \
                         [Z_LEFT_FAR] = {Z_CLOSET, Z_BED, Z_LIVING_LEFT}, [Z_LEFT_NEAR] = {-1, -1, -1}, [Z_RIGHT_FAR] = {Z_CLOSET, Z_BED, Z_LIVING_RIGHT}, [Z_RIGHT_NEAR] = {-1, -1, -1}, \
                         [Z_KITCHEN] = {-1, -1, -1}, [Z_CLOSET] = {Z_LIVING_LEFT, Z_LIVING_RIGHT, -1}, [Z_BED] = {Z_LIVING_LEFT, Z_LIVING_RIGHT, -1}}
static const signed char routes[SIDE_COUNT][10][3] = {
    [SIDE_BONNIE] = {[Z_CENTER] = {Z_LIVING_LEFT, -1, -1}, [Z_LIVING_LEFT] = {Z_CENTER, Z_LEFT_FAR, -1}, [Z_LIVING_RIGHT] = {-1, -1, -1}, [Z_LEFT_FAR] = {Z_LEFT_NEAR, Z_LIVING_LEFT, -1},
                     [Z_LEFT_NEAR] = {Z_LEFT_FAR, -1, -1}, [Z_RIGHT_FAR] = {-1, -1, -1}, [Z_RIGHT_NEAR] = {-1, -1, -1}, [Z_KITCHEN] = {-1, -1, -1}, [Z_CLOSET] = {-1, -1, -1}, [Z_BED] = {-1, -1, -1}},
    [SIDE_CHICA] = {[Z_CENTER] = {Z_LIVING_RIGHT, -1, -1}, [Z_LIVING_LEFT] = {-1, -1, -1}, [Z_LIVING_RIGHT] = {Z_KITCHEN, Z_RIGHT_FAR, Z_CENTER}, [Z_LEFT_FAR] = {-1, -1, -1},
                    [Z_LEFT_NEAR] = {-1, -1, -1}, [Z_RIGHT_FAR] = {Z_RIGHT_NEAR, Z_LIVING_RIGHT, -1}, [Z_RIGHT_NEAR] = {Z_RIGHT_FAR, -1, -1}, [Z_KITCHEN] = {Z_LIVING_RIGHT, -1, -1}, [Z_CLOSET] = {-1, -1, -1}, [Z_BED] = {-1, -1, -1}},
    [SIDE_FOXY] = {[Z_CENTER] = {Z_LIVING_LEFT, Z_LIVING_RIGHT, -1}, [Z_LIVING_LEFT] = {Z_LIVING_RIGHT, Z_LEFT_FAR, Z_CENTER}, [Z_LIVING_RIGHT] = {Z_LIVING_LEFT, Z_RIGHT_FAR, Z_CENTER}, [Z_LEFT_FAR] = {Z_CLOSET, -1, -1},
                   [Z_LEFT_NEAR] = {-1, -1, -1}, [Z_RIGHT_FAR] = {Z_CLOSET, -1, -1}, [Z_RIGHT_NEAR] = {-1, -1, -1}, [Z_KITCHEN] = {-1, -1, -1}, [Z_CLOSET] = {Z_CLOSET, -1, -1}, [Z_BED] = {-1, -1, -1}},
    [SIDE_FREDBEAR] = FREDBEAR_ROUTES,
    [SIDE_NIGHTMARE] = FREDBEAR_ROUTES};
static const wchar_t *const side_objects[SIDE_COUNT] = {NULL, L"Bonnie", L"Chica", L"foxy", L"Fredbear", L"Fredbear"};
static const wchar_t *const side_ai[SIDE_COUNT] = {NULL, L"Bonnie AI", L"Chica AI", L"Foxy AI", L"Fredbear AI", L"Fredbear AI"};
static const char *const zone_names[10] = {"CENTER", "LIVING ROOM L", "LIVING ROOM R", "LEFT HALL", "LEFT DOOR", "RIGHT HALL", "RIGHT DOOR", "KITCHEN", "CLOSET", "BED"};
static const short zone_x[10] = {512, 300, 724, 200, 200, 824, 824, 724, 512, 512};
static const short zone_y[10] = {150, 250, 250, 370, 490, 370, 490, 150, 490, 610};

static int current_zone(int who) {
    int x, y;
    if (!pos_read(obj(side_objects[who]), &x, &y)) return Z_NONE;
    int z = zone_of(x, y);
    return z == Z_NONE ? Z_CENTER : z;
}

static bool route_allows(int who, int from, int to) {
    if (from < 0) return false;
    for (int i = 0; i < 3; i++)
        if (routes[who][from][i] == to) return true;
    return false;
}

/* The host checks a move the same way the game's random rolls are limited. */
static void versus_move(int who, int to, uint32_t now) {
    if (who < SIDE_BONNIE || who >= SIDE_COUNT || to < 0 || to >= 10) return;
    int from = current_zone(who);
    if (!route_allows(who, from, to) || (int32_t)(now - move_ready_ms[who]) < 0) return;
    if (to == Z_LEFT_FAR && cnt_get(L"viewing left hall", 0)) return;
    if (to == Z_RIGHT_FAR && cnt_get(L"viewing right hall", 0)) return;
    BYTE *o = obj(side_objects[who]), *follow = obj(L"follow");
    move_ready_ms[who] = now + MOVE_COOLDOWN_MS;
    if (who == SIDE_FOXY && to == Z_CLOSET && from == Z_CLOSET) {
        av_write(o, 1, 1); /* the closet stage rises on this roll */
        return;
    }
    int x, y;
    if (!pos_read(obj(zones[to]), &x, &y)) return;
    pos_write(o, x + 1, y + (to == Z_LEFT_NEAR || to == Z_RIGHT_NEAR ? 1 : 0));
    av_write(o, 1, 0);
    if (who == SIDE_BONNIE) av_write(follow, to == Z_LEFT_FAR || to == Z_LEFT_NEAR ? 21 : 23, 1 + rand() % 3);
    if (who == SIDE_CHICA) av_write(follow, to == Z_RIGHT_FAR || to == Z_RIGHT_NEAR ? 22 : 24, 1 + rand() % 3);
    if (who == SIDE_FREDBEAR || who == SIDE_NIGHTMARE) fredbear_move_sound(to);
}

static void versus_host(const Output *o, uint32_t now) {
    bool taken[SIDE_COUNT];
    sides_taken(o, taken);
    if (taken[SIDE_NIGHTMARE]) cnt_set(L"shadow", 1);
    /* A character with no AI is parked off the map, so one a player controls needs at least 1. */
    for (int i = SIDE_BONNIE; i < SIDE_COUNT; i++)
        if (taken[i] && cnt_get(side_ai[i], 0) <= 0) {
            int x, y;
            cnt_set(side_ai[i], 1);
            if (pos_read(obj(L"living room center"), &x, &y)) pos_write(obj(side_objects[i]), x, y + 1);
        }
    for (int i = 0; i < o->command_count; i++) versus_move(o->commands[i].side, o->commands[i].zone, now);
}

static void hud_rect(OverlayScene *s, int x0, int y0, int x1, int y1, unsigned color, int alpha, int radius) {
    if (s->rect_count >= OVERLAY_MAX_RECTS) return;
    OverlayRect *r = &s->rects[s->rect_count++];
    r->x0 = x0;
    r->y0 = y0;
    r->x1 = x1;
    r->y1 = y1;
    r->color = color;
    r->alpha = alpha;
    r->radius = radius;
}

static void hud_text(OverlayScene *s, int x, int y, OverlayAlign align, unsigned color, const char *text) {
    if (s->text_count >= OVERLAY_MAX_TEXTS) return;
    OverlayText *t = &s->texts[s->text_count++];
    t->x = x;
    t->y = y;
    t->size = 1;
    t->align = align;
    t->color = color;
    snprintf(t->text, OVERLAY_TEXT_LEN, "%s", text);
}

static void hud_bar(OverlayScene *s, int x, int y, const char *label, int value, int full, unsigned color) {
    hud_rect(s, x - 6, y - 6, x + 190, y + 40, 0x101014, 200, 8);
    hud_text(s, x, y, ALIGN_LEFT, 0xFFFFFF, label);
    hud_rect(s, x, y + 22, x + 178, y + 32, 0x303038, 255, 2);
    hud_rect(s, x, y + 22, x + 178 * value / full, y + 32, color, 255, 2);
}

static const char *station_label(int station) {
    switch (station) {
    case STATION_LEFT: return "LEFT DOOR";
    case STATION_RIGHT: return "RIGHT DOOR";
    case STATION_CLOSET: return "CLOSET";
    case STATION_BED: return "BED";
    default: return "ROOM";
    }
}

static void draw_banner(const Output *o, OverlayScene *scene) {
    char line[OVERLAY_TEXT_LEN];
    if (o->rescue) {
        snprintf(line, sizeof line, "%s WAS TAKEN! LIGHT THE %s: %ds", o->rescue_name, station_label(o->rescue_station), o->rescue_left_ms / 1000 + 1);
        hud_rect(scene, 212, 16, 812, 58, 0x5A1010, 235, 8);
        hud_text(scene, 512, 28, ALIGN_CENTER, 0xFFFFFF, line);
    } else if (my_state == PLAYER_OUT) {
        hud_rect(scene, 312, 16, 712, 58, 0x101014, 220, 8);
        hud_text(scene, 512, 28, ALIGN_CENTER, 0xFFFFFF, "YOU'RE OUT. HELP YOUR FRIENDS BY VOICE.");
    }
}

static void draw_map(const Output *o, uint32_t now, OverlayScene *scene) {
    char line[OVERLAY_TEXT_LEN];
    int here = current_zone(side);
    hud_rect(scene, 0, 0, 1024, 768, 0x0A0608, 245, 0);
    snprintf(line, sizeof line, "YOU ARE %s", menu_side_name(MODE_VERSUS, side));
    hud_text(scene, 512, 36, ALIGN_CENTER, 0xFFFFFF, line);
    for (int z = 0; z < 10; z++) {
        bool usable = false;
        for (int r = 0; r < 3; r++) usable = usable || (here >= 0 && routes[side][here][r] == z);
        if (z != here && !usable && zone_x[z] == 512 && zone_y[z] == 610) continue;
        unsigned color = z == here ? 0x9A1C1C : usable ? 0x5A1010 : 0x241212;
        hud_rect(scene, zone_x[z] - 92, zone_y[z] - 26, zone_x[z] + 92, zone_y[z] + 26, color, 255, 8);
        hud_text(scene, zone_x[z], zone_y[z] - 9, ALIGN_CENTER, usable || z == here ? 0xFFFFFF : 0x8A7A7A, zone_names[z]);
    }
    int wait = (int)(map_cooldown_until - now);
    snprintf(line, sizeof line, wait > 0 ? "NEXT MOVE IN %dS" : "CLICK A LIT ROOM TO MOVE", wait / 1000 + 1);
    hud_text(scene, 512, 690, ALIGN_CENTER, 0xB89A9A, line);
    hud_text(scene, 512, 716, ALIGN_CENTER, 0xB89A9A, side == SIDE_FOXY && here == Z_CLOSET ? "CLICK CLOSET TO CREEP CLOSER" : "A SHUT DOOR SENDS YOU BACK");
    int y = 560;
    for (int i = 0; i < o->player_count && y < 660; i++) {
        if (o->players[i].side != SIDE_SURVIVOR || o->players[i].state != PLAYER_ALIVE) continue;
        snprintf(line, sizeof line, "%s: %s%s", o->players[i].name, station_label(o->players[i].station), o->players[i].input & (IN_HOLD_LEFT | IN_HOLD_RIGHT | IN_HOLD_CLOSET) ? " *HOLDING*" : "");
        hud_text(scene, 512, y + 74, ALIGN_CENTER, 0xFFFFFF, line);
        y += 24;
    }
}

void rules_map_click(int x, int y, uint32_t now) {
    if ((int32_t)(now - map_cooldown_until) < 0) return;
    int here = current_zone(side);
    for (int z = 0; z < 10; z++) {
        if (x < zone_x[z] - 92 || x >= zone_x[z] + 92 || y < zone_y[z] - 26 || y >= zone_y[z] + 26) continue;
        if (!route_allows(side, here, z)) continue;
        map_cooldown_until = now + MOVE_COOLDOWN_MS;
        if (host) versus_move(side, z, now);
        else rules_command(z);
    }
}

static void draw_hud(const Output *o, const Local *l, uint32_t now, OverlayScene *scene) {
    int frame = game_frame();
    if ((frame == FRAME_PLUSHTRAP || frame == FRAME_BB) && mode == MODE_COOP && side == SIDE_EARS) {
        hud_rect(scene, 0, 0, 1024, 768, 0x000000, 255, 0);
        hud_text(scene, 512, 360, ALIGN_CENTER, 0xFFFFFF, "YOU HOLD THE LIGHT. YOU CAN'T SEE IT.");
        hud_text(scene, 512, 392, ALIGN_CENTER, 0xB89A9A, "EYES TELLS YOU WHEN. HOLD CTRL.");
        return;
    }
    if (!l->in_level || l->scaring) return;
    if (rules_map_open()) {
        draw_map(o, now, scene);
        return;
    }
    draw_banner(o, scene);
    if (mode == MODE_EXTENDED) {
        hud_bar(scene, 16, 690, o->battery > 0 ? "BATTERY" : "BATTERY EMPTY", o->battery, SYNC_BATTERY_FULL, o->battery > 200 ? 0x46C86E : 0xE03C3C);
        if (grip < GRIP_FULL && my_state == PLAYER_ALIVE) hud_bar(scene, 220, 690, grip_spent ? "ARMS TIRED" : "GRIP", grip, GRIP_FULL, grip_spent ? 0xE03C3C : 0xE0B83C);
    }
    if (mode == MODE_COOP) {
        hud_rect(scene, 10, 716, 110, 754, 0x101014, 200, 8);
        hud_text(scene, 60, 726, ALIGN_CENTER, 0xFFFFFF, side == SIDE_EYES ? "EYES" : "EARS");
    }
}

void rules_scene(const Output *o, const Local *l, uint32_t now, OverlayScene *scene) {
    static uint32_t last;
    static double left_open = 1, right_open = 1, closet_open = 1;
    uint32_t dt = now - last;
    int stack[STATION_BED + 1] = {0};
    int av0 = -1;
    last = now;
    if (dt > 200) dt = 200;
    draw_hud(o, l, now, scene);
    if (!l->in_level || l->scaring || rules_map_open()) {
        left_open = right_open = closet_open = 1;
        return;
    }
    av_read(obj(L"follow"), 0, &av0);
    left_open = swing(left_open, o->others & IN_HOLD_LEFT, dt);
    right_open = swing(right_open, o->others & IN_HOLD_RIGHT, dt);
    closet_open = swing(closet_open, o->others & IN_HOLD_CLOSET, dt);
    if (av0 == 2) add_door(scene, &left_door, true, left_open);
    if (av0 == 5) add_door(scene, &right_door, false, right_open);
    if (av0 == 0) add_door(scene, &closet_gap_center, true, closet_open);
    if (av0 == 5) add_door(scene, &closet_gap_right, true, closet_open);
    for (int i = 0; i < o->player_count; i++)
        if (o->players[i].state == PLAYER_ALIVE && o->players[i].station != STATION_NONE) add_bubble(scene, o->players[i].station, av0, &o->players[i], stack);
}

/* Each extra survivor makes the animatronics tougher. A value the game wrote since last time is the
 * new base, so its own hour-by-hour rises keep working. */
static void scale_difficulty(int survivors) {
    static const wchar_t *const names[5] = {L"Bonnie AI", L"Chica AI", L"Foxy AI", L"Freddy AI", L"Fredbear AI"};
    static const int step[5] = {3, 3, 2, 1, 3};
    for (int i = 0; i < 5; i++) {
        int value = cnt_get(names[i], 0);
        if (value != ai_written[i]) ai_base[i] = value;
        if (ai_base[i] <= 0) {
            ai_written[i] = value;
            continue;
        }
        int scaled = ai_base[i] + step[i] * (survivors > 1 ? survivors - 1 : 0);
        if (scaled > 20) scaled = 20;
        cnt_set(names[i], scaled);
        ai_written[i] = scaled;
    }
}

/* Tired arms: holding wears out your grip, and sharing a door makes it last twice as long. */
static void update_grip(const Output *o, const Local *l, uint32_t dt) {
    bool holding = l->input & (IN_HOLD_LEFT | IN_HOLD_RIGHT | IN_HOLD_CLOSET);
    if (holding) {
        int rate = GRIP_FULL * (int)dt / GRIP_DRAIN_MS;
        if (o->others & l->input & (IN_HOLD_LEFT | IN_HOLD_RIGHT | IN_HOLD_CLOSET)) rate /= 2;
        grip -= rate;
    } else {
        grip += GRIP_FULL * (int)dt / GRIP_RECOVER_MS;
    }
    if (grip > GRIP_FULL) grip = GRIP_FULL;
    if (grip <= 0) {
        grip = 0;
        grip_spent = true;
    }
    if (grip >= GRIP_READY) grip_spent = false;
    key_block(VK_SHIFT_KEY, grip_spent);
}

static void update_blocks(const Output *o, const Local *l, uint32_t dt) {
    int frame = game_frame();
    bool minigame = frame == FRAME_PLUSHTRAP || frame == FRAME_BB;
    bool no_light = false;
    /* An empty battery stays dead until it has real charge, or the light would stutter. */
    static bool battery_dead;
    if (o->battery <= 0) battery_dead = true;
    if (o->battery >= BATTERY_READY) battery_dead = false;
    if (mode == MODE_EXTENDED && l->in_level && battery_dead) no_light = true;
    if (mode == MODE_COOP && l->in_level && side == SIDE_EARS) no_light = true;
    if (mode == MODE_COOP && minigame && side == SIDE_EYES) no_light = true;
    key_block(VK_CTRL_KEY, no_light);
    if (mode == MODE_EXTENDED && l->in_level) update_grip(o, l, dt);
    else key_block(VK_SHIFT_KEY, false);
}

/* The game's exit from a Nightmare scare beats the mod's next tick, so a death would never be
 * reported, and its own frame ends in Extras. Here the exit waits for the report, and the scare
 * ends at game over like any other. */
static void versus_scare(const Local *l, uint32_t now) {
    static uint32_t calm_ms, nightmare_ms;
    int frame = game_frame();
    if (mode != MODE_VERSUS) return;
    if (frame == FRAME_LEVEL) {
        if (!l->scaring) calm_ms = now;
        if (now - calm_ms >= SCARE_REPORT_MS) evg_enable(RULE_NIGHTMARE_EXIT);
        else evg_disable(RULE_NIGHTMARE_EXIT);
    }
    if (frame != FRAME_NIGHTMARE) nightmare_ms = now;
    else if (now - nightmare_ms >= NIGHTMARE_SCARE_MS) game_goto(FRAME_GAME_OVER);
}

void rules_apply(const Output *o, const Local *l, uint32_t now) {
    static uint32_t last;
    uint32_t dt = last ? now - last : 0;
    int frame = game_frame();
    last = now;
    if (dt > 200) dt = 200;
    my_state = o->my_state;
    if (host && l->in_level && my_state != PLAYER_ALIVE) resume_needed = !o->team_lost;
    if (host && my_state == PLAYER_ALIVE && !away) resume_needed = false;
    if (away) return;
    if (l->in_level && o->apply_kill != KILL_NONE) scare_on = true;
    role_rules(o);
    versus_scare(l, now);
    update_blocks(o, l, dt);
    if (is_ghost()) {
        key_force(VK_SHIFT_KEY, false);
        key_force(VK_CTRL_KEY, false);
    } else {
        shared_keys(o, l);
    }
    if (host && resume_ticks > 0) {
        resume_ticks--;
        apply_rows(night_rows, COUNT(night_rows), saved);
    }
    if (host && l->in_level && mode == MODE_EXTENDED) scale_difficulty(o->survivors);
    if (host && l->in_level && mode == MODE_VERSUS) versus_host(o, now);
    if (o->apply_kill != KILL_NONE && l->in_level) rules_force_kill(o->apply_kill);
    /* The host's copy of rule 400 ends in Foxy's kill. Mirroring would cancel that on a client and
     * leave it pushed off the bed each time, so a client runs the outcome itself. */
    if (!host && !is_ghost() && l->in_level && l->kill == KILL_NONE) {
        int camped = 0;
        if (av_read(obj(L"Fredbear"), 14, &camped) && camped >= 15) rules_force_kill(KILL_FOXY);
    }
    if (host) {
        if (l->in_level) host_merge(o, l, now);
    } else if (o->apply_values) {
        if (l->in_level && !force_active) {
            play_move_sounds(o->values);
            apply_rows(night_rows, COUNT(night_rows), o->values);
        } else if (frame == FRAME_PLUSHTRAP || frame == FRAME_BB) {
            /* The host zeroes this as its light comes on, and the same packet turns this player's light
             * on. The game needs the old value for one frame to show what the light caught. */
            static bool was_lit;
            bool lit = o->others & IN_LIGHT;
            for (size_t i = 0; i < M_X; i++)
                if (i != M_ACTIVE || !lit || was_lit) row_write(&mini_rows[i], o->values[i]);
            was_lit = lit;
            mirror_trap(o->values);
        } else if (frame == FRAME_WHAT_NIGHT) {
            apply_rows(night_title_rows, COUNT(night_title_rows), o->values);
        }
    }
    if (l->in_level) snprintf(debug_line, sizeof debug_line, "kill=%d force=%d others=%03x input=%03x grip=%d", l->kill, force_active, o->others, l->input, grip);
}
