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
static const Box leave_button = {FIELD_X0, 596, FIELD_X1, 640};

static bool inside(const Box *b, int x, int y) { return x >= b->x0 && x < b->x1 && y >= b->y0 && y < b->y1; }

static Box room_row(int index) {
    Box b = {FIELD_X0, 318 + index * (ROW_H + 6), FIELD_X1, 318 + index * (ROW_H + 6) + ROW_H};
    return b;
}

void menu_init(Menu *m, const char *name) {
    memset(m, 0, sizeof *m);
    strncpy(m->name, name, SYNC_NAME_LEN - 1);
    lobby_clean_name(m->name, SYNC_NAME_LEN);
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
    text(s, FIELD_X0, 156, 1, ALIGN_LEFT, COLOR_DIM, "YOUR NAME");
    field(s, m, &name_field, m->name, "", m->focus == FOCUS_NAME, now_ms);
    button(s, m, &host_button, "HOST A ROOM");
    text(s, FIELD_X0, 294, 1, ALIGN_LEFT, COLOR_DIM, "ROOMS ON THIS WI-FI");
    if (count == 0) text(s, (PANEL_X0 + PANEL_X1) / 2, 370, 1, ALIGN_CENTER, COLOR_DIM, "Looking for rooms...");
    for (int i = 0; i < count; i++) {
        const Room *room = lobby_room(lobby, i);
        Box row = room_row(i);
        char label[OVERLAY_TEXT_LEN];
        rect(s, &row, inside(&row, m->hover_x, m->hover_y) ? COLOR_HOT : COLOR_ROW, 255, 6);
        snprintf(label, sizeof label, "%s - %d/%d - JOIN", room->name, room->players, SYNC_MAX_PLAYERS);
        text(s, row.x0 + 12, row.y0 + 13, 1, ALIGN_LEFT, COLOR_TEXT, label);
    }
    text(s, FIELD_X0, 526, 1, ALIGN_LEFT, COLOR_DIM, "OR JOIN BY ADDRESS");
    field(s, m, &address_field, m->address, "192.168.1.5", m->focus == FOCUS_ADDRESS, now_ms);
    button(s, m, &address_join, "JOIN");
}

static void draw_room(const Menu *m, OverlayScene *s) {
    char line[OVERLAY_TEXT_LEN];
    int mid = (PANEL_X0 + PANEL_X1) / 2;
    if (m->room == ROOM_HOSTING) {
        text(s, mid, 170, 1, ALIGN_CENTER, COLOR_TEXT, "YOUR ROOM IS OPEN");
        snprintf(line, sizeof line, "Address: %s", m->my_address);
        text(s, mid, 214, 1, ALIGN_CENTER, COLOR_DIM, line);
        text(s, mid, 360, 1, ALIGN_CENTER, COLOR_TEXT, "Close this panel and pick");
        text(s, mid, 388, 1, ALIGN_CENTER, COLOR_TEXT, "New Game or Continue.");
        text(s, mid, 416, 1, ALIGN_CENTER, COLOR_DIM, "Everyone follows you in.");
    } else if (m->room == ROOM_JOINING) {
        text(s, mid, 170, 1, ALIGN_CENTER, COLOR_TEXT, "CONNECTING...");
    } else {
        snprintf(line, sizeof line, "IN %s'S ROOM", m->room_name);
        text(s, mid, 170, 1, ALIGN_CENTER, COLOR_TEXT, line);
        text(s, mid, 388, 1, ALIGN_CENTER, COLOR_DIM, "Waiting for the host to start.");
    }
    snprintf(line, sizeof line, "Players: %d", m->player_count);
    text(s, mid, 262, 1, ALIGN_CENTER, COLOR_TEXT, line);
    text(s, mid, 290, 1, ALIGN_CENTER, COLOR_DIM, m->players);
    button(s, m, &leave_button, m->room == ROOM_HOSTING ? "CLOSE ROOM" : "LEAVE ROOM");
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
    button(s, m, &close_button, "X");
    if (m->room == ROOM_NONE) draw_lobby(m, lobby, now_ms, s);
    else draw_room(m, s);
    if (m->status[0]) text(s, (PANEL_X0 + PANEL_X1) / 2, PANEL_Y1 - 16, 1, ALIGN_CENTER, 0xFFB060, m->status);
}

void menu_mouse(Menu *m, int x, int y) {
    m->hover_x = x;
    m->hover_y = y;
}

static void leave_field(Menu *m, MenuAction *action) {
    if (m->focus == FOCUS_NAME) {
        lobby_clean_name(m->name, SYNC_NAME_LEN);
        if (action) *action = ACTION_NAME_CHANGED;
    }
    m->focus = FOCUS_NONE;
}

bool menu_click(Menu *m, const Lobby *lobby, int x, int y, MenuAction *action) {
    *action = ACTION_NONE;
    if (!m->open) {
        if (!inside(&coop_button, x, y)) return false;
        m->open = true;
        m->status[0] = 0;
        return true;
    }
    leave_field(m, action);
    if (inside(&close_button, x, y)) {
        m->open = false;
        return true;
    }
    if (m->room != ROOM_NONE) {
        if (inside(&leave_button, x, y)) *action = ACTION_LEAVE;
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
    else m->open = false;
}
