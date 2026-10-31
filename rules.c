#include "rules.h"
#include "game.h"
#include "input.h"
#include "overlay.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define VK_SHIFT_KEY 16
#define VK_CTRL_KEY 17
#define FREDDY_KILL 80
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
    386, 387, 388, 389, 390, 391, 392, 393, 398, 401, 402, 403, 404, 405, 406, 407, 408, 432, 436, 437, 446, 447, 448, 449,
    451, 452, 453, 454, 455, 469, 470, 476, 477, 478};
/* Rules the host needs off because the mod merges these values from every player. */
static const short host_off_level[] = {48, 126, 192, 193, 194, 195};
static const short client_off_minigame[] = {3, 6, 8, 9, 44};

/* Host reads of `listening mode` that decide threats move to their own counters, one per side, so a
 * player listening at each door both count and the host's breathing audio stays its own. */
static const struct { short group, cond; const wchar_t *counter; } listen_reads[] = {
    {149, 2, L"test 2"}, {153, 0, L"test 2"}, {160, 3, L"test 2"}, {328, 1, L"test 2"}, {329, 1, L"test 2"},
    {150, 2, L"test 3"}, {155, 0, L"test 3"}, {166, 3, L"test 3"}, {326, 1, L"test 3"}, {327, 1, L"test 3"}};

typedef enum { ROW_COUNTER, ROW_X, ROW_Y, ROW_AV } RowKind;
typedef struct { const wchar_t *object; RowKind kind; int index; } Row;

static const Row night_rows[] = {
    {L"hour", ROW_COUNTER, 0}, {L"Night", ROW_COUNTER, 0}, {L"shadow", ROW_COUNTER, 0}, {L"Bonnie AI", ROW_COUNTER, 0},
    {L"Chica AI", ROW_COUNTER, 0}, {L"Foxy AI", ROW_COUNTER, 0}, {L"Freddy AI", ROW_COUNTER, 0}, {L"Fredbear AI", ROW_COUNTER, 0},
    {L"Freddy counter", ROW_COUNTER, 0}, {L"Bonnie", ROW_X, 0}, {L"Bonnie", ROW_Y, 0}, {L"Chica", ROW_X, 0}, {L"Chica", ROW_Y, 0},
    {L"foxy", ROW_X, 0}, {L"foxy", ROW_Y, 0}, {L"Fredbear", ROW_X, 0}, {L"Fredbear", ROW_Y, 0}, {L"foxy", ROW_AV, 2},
    {L"Bonnie", ROW_AV, 6}, {L"Chica", ROW_AV, 6}, {L"Fredbear", ROW_AV, 0}, {L"Fredbear", ROW_AV, 6}, {L"Fredbear", ROW_AV, 7}};
enum { V_HOUR, V_NIGHT, V_SHADOW, V_BONNIE_AI, V_CHICA_AI, V_FOXY_AI, V_FREDDY_AI, V_FREDBEAR_AI, V_FREDDY, V_BONNIE_X, V_BONNIE_Y, V_CHICA_X, V_CHICA_Y, V_FOXY_X, V_FOXY_Y, V_FRED_X, V_FRED_Y };

static const Row mini_rows[] = {{L"Counter", ROW_COUNTER, 0}, {L"becoming active", ROW_COUNTER, 0}, {L"random", ROW_COUNTER, 0}, {L"is aggressive?", ROW_COUNTER, 0}, {L"plushtrap", ROW_X, 0}, {L"plushtrap", ROW_Y, 0}};
enum { M_COUNTER, M_ACTIVE, M_RANDOM, M_AGGRESSIVE, M_X, M_Y };

static const Row night_title_rows[] = {{L"Night", ROW_COUNTER, 0}, {L"shadow", ROW_COUNTER, 0}};

static bool host;
static int my_kill;
static int forced_kind;
static int last_av0 = -1;
static bool force_active;
static int *patched_literal;
static int patched_literal_old;
static BYTE *patched_for;
static uint32_t last_closet_ms, last_fred_closet_ms, last_fred_bed_ms;
static int side_restore = -1;
static char debug_line[160];

void rules_init(bool is_host) { host = is_host; }
const char *rules_debug(void) { return debug_line; }
bool rules_host_off_is_active(void) { return evg_is_disabled(host_off_level[1]) || evg_is_disabled(client_off_level[1]); }

static bool row_read(const Row *r, int32_t *out) {
    BYTE *o = obj(r->object);
    int a, b;
    if (!o) return false;
    switch (r->kind) {
    case ROW_COUNTER: return cnt_read(o, (int *)out);
    case ROW_X: if (!pos_read(o, &a, &b)) return false; *out = a; return true;
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
    case ROW_Y: if (pos_read(o, &x, &y)) pos_write(o, x, value); break;
    case ROW_AV: av_write(o, r->index, value); break;
    }
}

static void disable_all(const short *list, size_t n) {
    for (size_t i = 0; i < n; i++) evg_disable(list[i]);
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
    bool ctrl = key_real(VK_CTRL_KEY), shift = key_real(VK_SHIFT_KEY);
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
        return;
    }
    l->in_level = true;
    l->station = station_of(av0);
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
static void play_move_sounds(const int32_t *v) {
    BYTE *follow = obj(L"follow");
    int x, y;
    if (pos_read(obj(L"Bonnie"), &x, &y) && (x != v[V_BONNIE_X] || y != v[V_BONNIE_Y])) {
        int z = zone_of(v[V_BONNIE_X], v[V_BONNIE_Y]);
        if (z == Z_LEFT_FAR || z == Z_LEFT_NEAR) av_write(follow, 21, 1 + rand() % 4);
        else if (z == Z_CENTER || z == Z_LIVING_LEFT) av_write(follow, 23, 1 + rand() % 3);
    }
    if (pos_read(obj(L"Chica"), &x, &y) && (x != v[V_CHICA_X] || y != v[V_CHICA_Y])) {
        int z = zone_of(v[V_CHICA_X], v[V_CHICA_Y]);
        if (z == Z_RIGHT_FAR || z == Z_RIGHT_NEAR) av_write(follow, 22, 1 + rand() % 4);
        else if (z != Z_NONE) av_write(follow, 24, 1 + rand() % 3);
    }
    if (pos_read(obj(L"foxy"), &x, &y) && (x != v[V_FOXY_X] || y != v[V_FOXY_Y])) {
        int z = zone_of(v[V_FOXY_X], v[V_FOXY_Y]);
        if (z == Z_LIVING_LEFT) av_write(follow, 11, 1 + rand() % 2);
        else if (z == Z_LIVING_RIGHT) av_write(follow, 17, 1 + rand() % 2);
    }
}

static void apply_rows(const Row *rows, size_t n, const int32_t *values) {
    for (size_t i = 0; i < n; i++) row_write(&rows[i], values[i]);
}

/* Merges every other player's input into the host's simulation. */
static void host_merge(const Output *o, const Local *l, uint32_t now) {
    int av0 = 0;
    bool ctrl = key_real(VK_CTRL_KEY);
    uint16_t others = o->others;
    av_read(obj(L"follow"), 0, &av0);

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

/* A door held by a friend closes on this player's screen through the game's own animation. */
static void shared_keys(const Output *o, const Local *l) {
    int frame = game_frame();
    bool shift = false, ctrl = false;
    if (frame == FRAME_PLUSHTRAP || frame == FRAME_BB) ctrl = o->others & IN_LIGHT;
    if (l->in_level) {
        int av0 = 0;
        av_read(obj(L"follow"), 0, &av0);
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

static void add_door(OverlayScene *scene, const OverlayQuad *door, bool left, double open) {
    if (open >= 1) return;
    OverlayQuad q = *door;
    q.color = DOOR_COLOR;
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
    switch (station) {
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

void rules_scene(const Output *o, const Local *l, uint32_t now, OverlayScene *scene) {
    static uint32_t last;
    static double left_open = 1, right_open = 1;
    uint32_t dt = now - last;
    int stack[STATION_BED + 1] = {0};
    int av0 = -1;
    last = now;
    if (dt > 200) dt = 200;
    if (!l->in_level || l->kill != KILL_NONE) {
        left_open = right_open = 1;
        return;
    }
    av_read(obj(L"follow"), 0, &av0);
    left_open = swing(left_open, o->others & IN_HOLD_LEFT, dt);
    right_open = swing(right_open, o->others & IN_HOLD_RIGHT, dt);
    if (av0 == 2) add_door(scene, &left_door, true, left_open);
    if (av0 == 5) add_door(scene, &right_door, false, right_open);
    for (int i = 0; i < o->player_count; i++) add_bubble(scene, o->players[i].station, av0, &o->players[i], stack);
}

void rules_apply(const Output *o, const Local *l, uint32_t now) {
    int frame = game_frame();
    shared_keys(o, l);
    if (o->apply_kill != KILL_NONE && l->in_level) rules_force_kill(o->apply_kill);
    if (host) {
        if (l->in_level) host_merge(o, l, now);
    } else if (o->apply_values) {
        if (l->in_level && !force_active) {
            play_move_sounds(o->values);
            apply_rows(night_rows, COUNT(night_rows), o->values);
        } else if (frame == FRAME_PLUSHTRAP || frame == FRAME_BB) {
            int x, y;
            if (pos_read(obj(L"plushtrap"), &x, &y) && (x != o->values[M_X] || y != o->values[M_Y])) {
                /* The game's own move rule plays the footstep for this step. */
                cnt_set(L"random", o->values[M_RANDOM]);
                cnt_set(L"move", 1);
            }
            apply_rows(mini_rows, COUNT(mini_rows), o->values);
        } else if (frame == FRAME_WHAT_NIGHT) {
            apply_rows(night_title_rows, COUNT(night_title_rows), o->values);
        }
    }
    snprintf(debug_line, sizeof debug_line, "kill=%d force=%d others=%03x", l->kill, force_active, o->others);
}
