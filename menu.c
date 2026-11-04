#include "menu.h"
#include <stdio.h>
#include <string.h>

#define PANEL_X0 252
#define PANEL_X1 772
#define PANEL_Y0 104
#define PANEL_Y1 664
#define FIELD_X0 (PANEL_X0 + 28)
#define FIELD_X1 (PANEL_X1 - 28)
#define ROW_H 44
#define COLOR_PANEL 0x0C0506
#define COLOR_EDGE 0x7A1414
#define COLOR_BUTTON 0x5A1010
#define COLOR_HOT 0x9A1C1C
#define COLOR_FIELD 0x241212
#define COLOR_ROW 0x1C0D0E
#define COLOR_TEXT 0xFFFFFF
#define COLOR_DIM 0xB89A9A

typedef struct { int x0, y0, x1, y1; } Box;

static const Box coop_button = {422, 628, 602, 672};
static const Box close_button = {PANEL_X1 - 52, PANEL_Y0 + 12, PANEL_X1 - 16, PANEL_Y0 + 48};
static const Box name_field = {FIELD_X0, 176, FIELD_X1, 216};
static const Box host_button = {FIELD_X0, 228, FIELD_X1, 272};
static const Box address_field = {FIELD_X0, 548, FIELD_X1 - 112, 588};
static const Box address_join = {FIELD_X1 - 100, 548, FIELD_X1, 588};
static const Box leave_button = {FIELD_X0, 596, FIELD_X1 - 236, 640};
static const Box settings_button = {FIELD_X1 - 224, 596, FIELD_X1 - 112, 640};
static const Box github_button = {FIELD_X1 - 100, 596, FIELD_X1, 640};
/* The jumpscare volume slider: the whole box takes clicks, the track runs between its ends. */
static const Box volume_slider = {FIELD_X0, 248, FIELD_X1 - 96, 296};
static const Box jumpscares_toggle = {FIELD_X0, 316, FIELD_X1, 356};
#define SLIDER_X0 (FIELD_X0 + 8)
#define SLIDER_X1 (FIELD_X1 - 104)
#define KNOB_HALF 8
static const Box invite_button = {FIELD_X0, 512, FIELD_X1, 552};
static const Box mode_prev = {FIELD_X0, 214, FIELD_X0 + 44, 254};
static const Box mode_next = {FIELD_X1 - 44, 214, FIELD_X1, 254};
static const Box standing_toggle = {FIELD_X0, 322, FIELD_X1, 362};
static const Box side_prev = {FIELD_X0, 396, FIELD_X0 + 44, 436};
static const Box side_next = {FIELD_X1 - 44, 396, FIELD_X1, 436};

static const char *const mode_names[MODE_COUNT] = {"DEFAULT", "EXTENDED", "TRUE COOPERATIVE", "VERSUS"};
static const char *const mode_lines[MODE_COUNT][2] = {
    {"The normal game, in the same room.", "One death ends the night for everyone."},
    {"Tougher animatronics, shared battery, tired arms.", "One team revive per night."},
    {"EYES see but can't hear breathing.", "EARS hear but have no flashlight."},
    {"Play as an animatronic, or survive them.", "Pick your side below."}};
static const char *const side_names[MODE_COUNT][SIDE_COUNT] = {
    {"", "", "", "", "", ""}, {"", "", "", "", "", ""}, {"EYES", "EARS", "", "", "", ""}, {"SURVIVOR", "BONNIE", "CHICA", "FOXY", "FREDBEAR", "NIGHTMARE"}};

const char *menu_mode_name(int mode) { return mode >= 0 && mode < MODE_COUNT ? mode_names[mode] : ""; }
const char *menu_side_name(int mode, int side) { return mode >= 0 && mode < MODE_COUNT && side >= 0 && side < SIDE_COUNT ? side_names[mode][side] : ""; }
int menu_side_count(int mode) { return mode == MODE_COOP ? 2 : mode == MODE_VERSUS ? SIDE_COUNT : 0; }

static bool inside(const Box *b, int x, int y) { return x >= b->x0 && x < b->x1 && y >= b->y0 && y < b->y1; }

static Box room_row(int index) {
    Box b = {FIELD_X0, 318 + index * (ROW_H + 6), FIELD_X1, 318 + index * (ROW_H + 6) + ROW_H};
    return b;
}

static Box friend_row(int index) {
    Box b = {FIELD_X0, 206 + index * (ROW_H + 6), FIELD_X1, 206 + index * (ROW_H + 6) + ROW_H};
    return b;
}

void menu_init(Menu *m, const char *name) {
    memset(m, 0, sizeof *m);
    m->jumpscare_volume = 100;
    strncpy(m->name, name, SYNC_NAME_LEN - 1);
    lobby_clean_name(m->name, SYNC_NAME_LEN);
    strcpy(m->last_name, m->name);
}

void menu_set_status(Menu *m, const char *text) {
    strncpy(m->status, text, MENU_STATUS_LEN - 1);
    m->status[MENU_STATUS_LEN - 1] = 0;
}

static void rect(OverlayScene *s, const Box *b, unsigned color, int alpha, int radius) {
    if (s->rect_count >= OVERLAY_MAX_RECTS) return;
    OverlayRect *r = &s->rects[s->rect_count++];
    r->x0 = b->x0;
    r->y0 = b->y0;
    r->x1 = b->x1;
    r->y1 = b->y1;
    r->color = color;
    r->alpha = alpha;
    r->radius = radius;
}

static void text(OverlayScene *s, int x, int y, int size, OverlayAlign align, unsigned color, const char *value) {
    if (s->text_count >= OVERLAY_MAX_TEXTS) return;
    OverlayText *t = &s->texts[s->text_count++];
    t->x = x;
    t->y = y;
    t->size = size;
    t->align = align;
    t->color = color;
    strncpy(t->text, value, OVERLAY_TEXT_LEN - 1);
}

static void button(OverlayScene *s, const Menu *m, const Box *b, const char *label) {
    rect(s, b, inside(b, m->hover_x, m->hover_y) ? COLOR_HOT : COLOR_BUTTON, 255, 8);
    text(s, (b->x0 + b->x1) / 2, (b->y0 + b->y1) / 2 - 9, 1, ALIGN_CENTER, COLOR_TEXT, label);
}

static void field(OverlayScene *s, const Menu *m, const Box *b, const char *value, const char *hint, bool focused, uint32_t now_ms) {
    char shown[OVERLAY_TEXT_LEN];
    rect(s, b, focused ? 0x3A1818 : COLOR_FIELD, 255, 6);
    if (!value[0] && !focused) {
        text(s, b->x0 + 12, b->y0 + 11, 1, ALIGN_LEFT, COLOR_DIM, hint);
        return;
    }
    snprintf(shown, sizeof shown, "%s%s", value, focused && (now_ms / 500) % 2 == 0 ? "_" : "");
    text(s, b->x0 + 12, b->y0 + 11, 1, ALIGN_LEFT, COLOR_TEXT, shown);
    (void)m;
}

static void draw_lobby(const Menu *m, const Lobby *lobby, uint32_t now_ms, OverlayScene *s) {
    int count = lobby_count(lobby);
    text(s, FIELD_X0, 158, 1, ALIGN_LEFT, COLOR_DIM, "YOUR NAME");
    field(s, m, &name_field, m->name, "", m->focus == FOCUS_NAME, now_ms);
    button(s, m, &host_button, "HOST A ROOM");
    text(s, FIELD_X0, 294, 1, ALIGN_LEFT, COLOR_DIM, m->steam ? "ROOMS ON THIS WI-FI AND STEAM FRIENDS" : "ROOMS ON THIS WI-FI");
    if (count == 0) text(s, (PANEL_X0 + PANEL_X1) / 2, 370, 1, ALIGN_CENTER, COLOR_DIM, "Looking for rooms...");
    for (int i = 0; i < count; i++) {
        const Room *room = lobby_room(lobby, i);
        Box row = room_row(i);
        char label[OVERLAY_TEXT_LEN];
        rect(s, &row, inside(&row, m->hover_x, m->hover_y) ? COLOR_HOT : COLOR_ROW, 255, 6);
        if (room->steam_lobby) snprintf(label, sizeof label, "%s - STEAM - JOIN", room->name);
        else snprintf(label, sizeof label, "%s - %d/%d - JOIN", room->name, room->players, SYNC_MAX_PLAYERS);
        text(s, row.x0 + 12, row.y0 + 13, 1, ALIGN_LEFT, COLOR_TEXT, label);
    }
    text(s, FIELD_X0, 526, 1, ALIGN_LEFT, COLOR_DIM, m->steam ? "OR JOIN BY ADDRESS OR STEAM LOBBY ID" : "OR JOIN BY ADDRESS");
    field(s, m, &address_field, m->address, "192.168.1.5", m->focus == FOCUS_ADDRESS, now_ms);
    button(s, m, &address_join, "JOIN");
}

static void draw_settings(const Menu *m, OverlayScene *s) {
    int mid = (PANEL_X0 + PANEL_X1) / 2;
    bool host = m->room == ROOM_HOSTING;
    char line[OVERLAY_TEXT_LEN];
    Box row = {FIELD_X0, 214, FIELD_X1, 254};
    rect(s, &row, COLOR_FIELD, 255, 6);
    text(s, mid, 225, 1, ALIGN_CENTER, COLOR_TEXT, mode_names[m->mode]);
    if (host) {
        button(s, m, &mode_prev, "<");
        button(s, m, &mode_next, ">");
    }
    text(s, mid, 266, 1, ALIGN_CENTER, COLOR_DIM, mode_lines[m->mode][0]);
    text(s, mid, 292, 1, ALIGN_CENTER, COLOR_DIM, mode_lines[m->mode][1]);
    snprintf(line, sizeof line, "LAST ONE STANDING: %s", m->last_standing ? "ON" : "OFF");
    if (host) {
        button(s, m, &standing_toggle, line);
    } else {
        rect(s, &standing_toggle, COLOR_FIELD, 255, 6);
        text(s, mid, 333, 1, ALIGN_CENTER, COLOR_TEXT, line);
    }
    if (menu_side_count(m->mode) > 0) {
        Box side = {FIELD_X0, 396, FIELD_X1, 436};
        text(s, mid, 374, 1, ALIGN_CENTER, COLOR_DIM, "YOUR SIDE");
        rect(s, &side, COLOR_FIELD, 255, 6);
        text(s, mid, 407, 1, ALIGN_CENTER, COLOR_TEXT, side_names[m->mode][m->side]);
        button(s, m, &side_prev, "<");
        button(s, m, &side_next, ">");
    }
}

static int slider_value(int x) {
    int v = ((x - SLIDER_X0) * 100 + (SLIDER_X1 - SLIDER_X0) / 2) / (SLIDER_X1 - SLIDER_X0);
    return v < 0 ? 0 : v > 100 ? 100 : v;
}

static void draw_settings_page(const Menu *m, OverlayScene *s) {
    int mid = (PANEL_X0 + PANEL_X1) / 2, v = m->jumpscare_volume;
    int knob = SLIDER_X0 + v * (SLIDER_X1 - SLIDER_X0) / 100;
    bool hot = m->dragging || inside(&volume_slider, m->hover_x, m->hover_y);
    char value[8];
    Box track = {SLIDER_X0, 268, SLIDER_X1, 276}, fill = {SLIDER_X0, 268, knob, 276};
    Box handle = {knob - KNOB_HALF, 256, knob + KNOB_HALF, 288};
    text(s, mid, 146, 1, ALIGN_CENTER, COLOR_TEXT, "SETTINGS");
    text(s, FIELD_X0, 214, 1, ALIGN_LEFT, COLOR_DIM, "JUMPSCARE VOLUME");
    rect(s, &track, COLOR_FIELD, 255, 4);
    if (v > 0) rect(s, &fill, COLOR_HOT, 255, 4);
    rect(s, &handle, hot ? COLOR_TEXT : COLOR_DIM, 255, 6);
    if (v == 0) snprintf(value, sizeof value, "OFF");
    else snprintf(value, sizeof value, "%d", v);
    text(s, (volume_slider.x1 + FIELD_X1) / 2, 263, 1, ALIGN_CENTER, COLOR_TEXT, value);
    button(s, m, &jumpscares_toggle, m->jumpscares_off ? "JUMPSCARES: OFF" : "JUMPSCARES: ON");
}

static void draw_room(const Menu *m, OverlayScene *s) {
    char line[OVERLAY_TEXT_LEN];
    int mid = (PANEL_X0 + PANEL_X1) / 2;
    if (m->room == ROOM_JOINING) {
        text(s, mid, 300, 1, ALIGN_CENTER, COLOR_TEXT, "CONNECTING...");
    } else {
        if (m->room == ROOM_HOSTING) snprintf(line, sizeof line, "YOUR ROOM - %s%s", m->my_address, m->steam_open ? " + STEAM" : "");
        else snprintf(line, sizeof line, "%s'S ROOM", m->room_name);
        text(s, mid, 146, 1, ALIGN_CENTER, COLOR_TEXT, line);
        snprintf(line, sizeof line, "%d: %s", m->player_count, m->players);
        text(s, mid, 178, 1, ALIGN_CENTER, COLOR_DIM, line);
        draw_settings(m, s);
        text(s, mid, 462, 1, ALIGN_CENTER, COLOR_DIM, m->room == ROOM_HOSTING ? "Close this and pick New Game or Continue." : "Waiting for the host to start.");
        text(s, mid, 488, 1, ALIGN_CENTER, COLOR_DIM, "Everyone follows the host in.");
        if (m->room == ROOM_HOSTING && m->steam_open) button(s, m, &invite_button, "INVITE STEAM FRIENDS");
    }
    button(s, m, &leave_button, m->room == ROOM_HOSTING ? "CLOSE ROOM" : "LEAVE ROOM");
}

static void draw_invites(const Menu *m, OverlayScene *s) {
    int mid = (PANEL_X0 + PANEL_X1) / 2;
    char line[OVERLAY_TEXT_LEN];
    text(s, mid, 146, 1, ALIGN_CENTER, COLOR_TEXT, "INVITE STEAM FRIENDS");
    snprintf(line, sizeof line, "LOBBY ID %s", m->lobby_id);
    text(s, mid, 178, 1, ALIGN_CENTER, COLOR_DIM, line);
    if (m->friend_count == 0) text(s, mid, 300, 1, ALIGN_CENTER, COLOR_DIM, "No Steam friends online.");
    for (int i = 0; i < m->friend_count && i < MENU_MAX_FRIENDS; i++) {
        Box row = friend_row(i);
        rect(s, &row, inside(&row, m->hover_x, m->hover_y) ? COLOR_HOT : COLOR_ROW, 255, 6);
        snprintf(line, sizeof line, "%s - %s", m->friends[i], m->invited[i] ? "INVITED" : "INVITE");
        text(s, row.x0 + 12, row.y0 + 13, 1, ALIGN_LEFT, m->invited[i] ? COLOR_DIM : COLOR_TEXT, line);
    }
    text(s, mid, 566, 1, ALIGN_CENTER, COLOR_DIM, "Friends start the game first, then accept in Steam.");
}

void menu_draw(const Menu *m, const Lobby *lobby, uint32_t now_ms, OverlayScene *s) {
    char label[OVERLAY_TEXT_LEN];
    if (!m->open) {
        if (m->room == ROOM_NONE) snprintf(label, sizeof label, "CO-OP");
        else snprintf(label, sizeof label, "CO-OP: %d", m->player_count);
        rect(s, &coop_button, inside(&coop_button, m->hover_x, m->hover_y) ? COLOR_HOT : COLOR_BUTTON, 235, 8);
        text(s, (coop_button.x0 + coop_button.x1) / 2, (coop_button.y0 + coop_button.y1) / 2 - 9, 1, ALIGN_CENTER, COLOR_TEXT, label);
        return;
    }
    Box panel = {PANEL_X0, PANEL_Y0, PANEL_X1, PANEL_Y1};
    Box edge = {PANEL_X0 - 3, PANEL_Y0 - 3, PANEL_X1 + 3, PANEL_Y1 + 3};
    rect(s, &edge, COLOR_EDGE, 255, 14);
    rect(s, &panel, COLOR_PANEL, 245, 12);
    text(s, PANEL_X0 + 28, PANEL_Y0 + 12, 2, ALIGN_LEFT, COLOR_TEXT, "CO-OP");
    text(s, PANEL_X0 + 148, PANEL_Y0 + 24, 1, ALIGN_LEFT, COLOR_DIM, "V" MOD_VERSION);
    button(s, m, &close_button, "X");
    button(s, m, &github_button, "GITHUB");
    /* Glyphs are about 9 view pixels wide on screen: after the version, and short of the X button. */
    if (m->steam_note[0]) text(s, PANEL_X0 + 214, PANEL_Y0 + 24, 1, ALIGN_LEFT, m->steam ? COLOR_DIM : 0xFFB060, m->steam_note);
    button(s, m, &settings_button, m->settings_open || m->invite_open ? "BACK" : "SETTINGS");
    if (m->settings_open) draw_settings_page(m, s);
    else if (m->invite_open && m->room == ROOM_HOSTING) draw_invites(m, s);
    else if (m->room == ROOM_NONE) draw_lobby(m, lobby, now_ms, s);
    else draw_room(m, s);
    if (m->status[0]) text(s, (PANEL_X0 + PANEL_X1) / 2, PANEL_Y1 - 16, 1, ALIGN_CENTER, 0xFFB060, m->status);
}

void menu_mouse(Menu *m, int x, int y) {
    m->hover_x = x;
    m->hover_y = y;
    if (m->dragging) m->jumpscare_volume = slider_value(x);
}

void menu_release(Menu *m, MenuAction *action) {
    *action = ACTION_NONE;
    if (!m->dragging) return;
    m->dragging = false;
    *action = ACTION_SETTINGS_CHANGED;
}

static void leave_field(Menu *m, MenuAction *action) {
    if (m->focus == FOCUS_NAME) {
        bool blank = true;
        for (const char *c = m->name; *c; c++) blank = blank && *c == ' ';
        if (blank) strcpy(m->name, m->last_name);
        lobby_clean_name(m->name, SYNC_NAME_LEN);
        strcpy(m->last_name, m->name);
        if (action) *action = ACTION_NAME_CHANGED;
    }
    m->focus = FOCUS_NONE;
}

bool menu_click(Menu *m, const Lobby *lobby, int x, int y, MenuAction *action) {
    *action = ACTION_NONE;
    if (!m->open) {
        if (!inside(&coop_button, x, y)) return false;
        m->open = true;
        m->settings_open = false;
        m->invite_open = false;
        m->dragging = false;
        m->status[0] = 0;
        return true;
    }
    leave_field(m, action);
    if (inside(&close_button, x, y)) {
        m->open = false;
        return true;
    }
    if (inside(&github_button, x, y)) {
        *action = ACTION_GITHUB;
        return true;
    }
    if (inside(&settings_button, x, y)) {
        if (m->invite_open) m->invite_open = false;
        else m->settings_open = !m->settings_open;
        return true;
    }
    if (m->invite_open && m->room != ROOM_HOSTING) m->invite_open = false;
    if (m->invite_open) {
        for (int i = 0; i < m->friend_count && i < MENU_MAX_FRIENDS; i++) {
            Box row = friend_row(i);
            if (inside(&row, x, y) && !m->invited[i]) {
                m->invite_index = i;
                *action = ACTION_INVITE;
            }
        }
        return true;
    }
    if (m->settings_open) {
        if (inside(&volume_slider, x, y)) {
            m->dragging = true;
            m->jumpscare_volume = slider_value(x);
        } else if (inside(&jumpscares_toggle, x, y)) {
            m->jumpscares_off = !m->jumpscares_off;
            *action = ACTION_SETTINGS_CHANGED;
        }
        return true;
    }
    if (m->room != ROOM_NONE) {
        int sides = menu_side_count(m->mode);
        if (inside(&leave_button, x, y)) *action = ACTION_LEAVE;
        if (m->room == ROOM_HOSTING && m->steam_open && inside(&invite_button, x, y)) {
            m->invite_open = true;
            m->friend_count = 0;
            memset(m->invited, 0, sizeof m->invited);
            *action = ACTION_INVITE_OPENED;
            return true;
        }
        if (m->room == ROOM_HOSTING) {
            if (inside(&mode_prev, x, y)) m->mode = (m->mode + MODE_COUNT - 1) % MODE_COUNT;
            if (inside(&mode_next, x, y)) m->mode = (m->mode + 1) % MODE_COUNT;
            if (inside(&standing_toggle, x, y)) m->last_standing = !m->last_standing;
        }
        sides = menu_side_count(m->mode);
        if (sides > 0) {
            if (inside(&side_prev, x, y)) m->side = (m->side + sides - 1) % sides;
            if (inside(&side_next, x, y)) m->side = (m->side + 1) % sides;
        }
        if (m->side >= (sides > 0 ? sides : 1)) m->side = 0;
        return true;
    }
    if (inside(&name_field, x, y)) m->focus = FOCUS_NAME;
    else if (inside(&address_field, x, y)) m->focus = FOCUS_ADDRESS;
    else if (inside(&host_button, x, y)) *action = ACTION_HOST;
    else if (inside(&address_join, x, y) && m->address[0]) *action = ACTION_JOIN_ADDRESS;
    else
        for (int i = 0; i < lobby_count(lobby); i++) {
            Box row = room_row(i);
            if (inside(&row, x, y)) {
                m->join_index = i;
                *action = ACTION_JOIN_ROOM;
            }
        }
    return true;
}

static void edit(char *value, int capacity, int c) {
    int len = (int)strlen(value);
    if (c == 8) {
        if (len > 0) value[len - 1] = 0;
    } else if (c >= 32 && c <= 126 && len < capacity - 1) {
        value[len] = (char)c;
        value[len + 1] = 0;
    }
}

void menu_char(Menu *m, int c, MenuAction *action) {
    *action = ACTION_NONE;
    if (c == 13) {
        leave_field(m, action);
        return;
    }
    if (m->focus == FOCUS_NAME) edit(m->name, SYNC_NAME_LEN, c);
    else if (m->focus == FOCUS_ADDRESS && (c == 8 || (c >= 32 && c <= 126 && c != ' '))) edit(m->address, LOBBY_ADDRESS_LEN, c);
}

void menu_escape(Menu *m, MenuAction *action) {
    *action = ACTION_NONE;
    if (m->focus != FOCUS_NONE) leave_field(m, action);
    else if (m->invite_open) m->invite_open = false;
    else if (m->settings_open) m->settings_open = false;
    else m->open = false;
    if (m->dragging && !m->settings_open) {
        m->dragging = false;
        *action = ACTION_SETTINGS_CHANGED;
    }
}
