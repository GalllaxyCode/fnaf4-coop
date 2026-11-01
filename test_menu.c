/* Tests for the room list and the CO-OP panel, using the panel's real coordinates. */
#include "lobby.h"
#include "menu.h"
#include <stdio.h>
#include <string.h>

static int failures, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); puts(""); } } while (0)

static Announce announce(uint32_t session, const char *name) {
    Announce a;
    memset(&a, 0, sizeof a);
    a.magic = LOBBY_MAGIC;
    a.version = SYNC_VERSION;
    a.players = 2;
    a.port = 47114;
    a.session = session;
    strncpy(a.name, name, SYNC_NAME_LEN - 1);
    return a;
}

static void test_rooms(void) {
    Lobby l;
    lobby_clear(&l);
    Announce a = announce(1, "Alice");
    CHECK(lobby_note(&l, &a, 10, 0) && lobby_count(&l) == 1, "room not added");
    CHECK(lobby_note(&l, &a, 10, 500) && lobby_count(&l) == 1, "room added twice");
    for (uint32_t i = 2; i < 10; i++) {
        Announce b = announce(i, "x");
        lobby_note(&l, &b, i, 600);
    }
    CHECK(lobby_count(&l) == LOBBY_MAX_ROOMS, "list overflow %d", lobby_count(&l));
    lobby_expire(&l, 600 + LOBBY_ROOM_TIMEOUT_MS);
    CHECK(lobby_count(&l) == 0, "stale rooms stayed %d", lobby_count(&l));
    a.magic = 7;
    CHECK(!lobby_note(&l, &a, 10, 0), "bad magic accepted");
    a = announce(1, "x");
    a.version = SYNC_VERSION + 1;
    CHECK(!lobby_note(&l, &a, 10, 0), "other mod version listed");
    a = announce(1, "abcdefghijklmnopqrstuvwxyz");
    memset(a.name, 'z', SYNC_NAME_LEN);
    lobby_note(&l, &a, 10, 0);
    CHECK(strlen(lobby_room(&l, 0)->name) < SYNC_NAME_LEN, "unterminated name");
}

static void test_names(void) {
    char a[16] = "  Al\x01ice  ", b[16] = "   ", c[16] = "\xD0\x98\xD0\xBC\xD1\x8F";
    lobby_clean_name(a, 16);
    lobby_clean_name(b, 16);
    lobby_clean_name(c, 16);
    CHECK(strcmp(a, "Alice") == 0, "got '%s'", a);
    CHECK(strcmp(b, "Player") == 0, "got '%s'", b);
    CHECK(strcmp(c, "Player") == 0, "cyrillic gave '%s'", c);
}

static void type(Menu *m, const char *s) {
    MenuAction act;
    for (; *s; s++) menu_char(m, *s, &act);
}

static void test_panel_flow(void) {
    Menu m;
    Lobby l;
    MenuAction act;
    lobby_clear(&l);
    menu_init(&m, "Alice");
    CHECK(!menu_click(&m, &l, 100, 100, &act), "title click taken by the menu");
    CHECK(menu_click(&m, &l, 500, 650, &act) && m.open, "button did not open the panel");
    CHECK(menu_click(&m, &l, 100, 100, &act) && m.open, "click outside went through the open panel");
    CHECK(menu_click(&m, &l, 500, 195, &act) && m.focus == FOCUS_NAME, "name field not focused");
    for (int i = 0; i < 5; i++) menu_char(&m, 8, &act);
    type(&m, "Bob the very long name indeed");
    CHECK(strlen(m.name) == SYNC_NAME_LEN - 1, "name not limited: %s", m.name);
    menu_char(&m, 13, &act);
    CHECK(act == ACTION_NAME_CHANGED && m.focus == FOCUS_NONE, "enter did not commit the name");
    menu_click(&m, &l, 500, 250, &act);
    CHECK(act == ACTION_HOST, "host button action %d", act);
}

static void test_join_paths(void) {
    Menu m;
    Lobby l;
    MenuAction act;
    Announce a = announce(1, "Alice"), b = announce(2, "Bob");
    lobby_clear(&l);
    lobby_note(&l, &a, 1, 0);
    lobby_note(&l, &b, 2, 0);
    menu_init(&m, "x");
    menu_click(&m, &l, 500, 650, &act);
    menu_click(&m, &l, 500, 390, &act);
    CHECK(act == ACTION_JOIN_ROOM && m.join_index == 1, "second room action %d index %d", act, m.join_index);
    menu_click(&m, &l, 700, 560, &act);
    CHECK(act == ACTION_NONE, "join with an empty address");
    menu_click(&m, &l, 400, 560, &act);
    type(&m, "192.168.1.5 ");
    CHECK(strcmp(m.address, "192.168.1.5") == 0, "address '%s'", m.address);
    menu_click(&m, &l, 700, 560, &act);
    CHECK(act == ACTION_JOIN_ADDRESS, "address join action %d", act);
}

static void test_room_settings(void) {
    Menu m;
    Lobby l;
    MenuAction act;
    lobby_clear(&l);
    menu_init(&m, "x");
    m.open = true;
    m.room = ROOM_HOSTING;
    menu_click(&m, &l, 720, 230, &act);
    CHECK(m.mode == MODE_EXTENDED, "next mode %d", m.mode);
    menu_click(&m, &l, 300, 230, &act);
    menu_click(&m, &l, 300, 230, &act);
    CHECK(m.mode == MODE_VERSUS, "previous wraps to %d", m.mode);
    menu_click(&m, &l, 500, 340, &act);
    CHECK(m.last_standing, "toggle did not switch on");
    menu_click(&m, &l, 720, 410, &act);
    menu_click(&m, &l, 720, 410, &act);
    menu_click(&m, &l, 720, 410, &act);
    CHECK(m.side == SIDE_FOXY, "side %d", m.side);
    menu_click(&m, &l, 720, 410, &act);
    menu_click(&m, &l, 720, 410, &act);
    CHECK(m.side == SIDE_NIGHTMARE && strcmp(menu_side_name(MODE_VERSUS, m.side), "NIGHTMARE") == 0, "side %d", m.side);
    menu_click(&m, &l, 720, 410, &act);
    CHECK(m.side == SIDE_SURVIVOR, "side did not wrap: %d", m.side);
    for (int i = 0; i < SIDE_FOXY; i++) menu_click(&m, &l, 720, 410, &act);
    menu_click(&m, &l, 720, 230, &act);
    CHECK(m.mode == MODE_DEFAULT && m.side == 0, "side kept in a mode without sides: %d", m.side);
    m.room = ROOM_JOINED;
    menu_click(&m, &l, 720, 230, &act);
    menu_click(&m, &l, 500, 340, &act);
    CHECK(m.mode == MODE_DEFAULT && m.last_standing, "client changed room settings");
    m.mode = MODE_COOP;
    menu_click(&m, &l, 720, 410, &act);
    CHECK(m.side == SIDE_EARS, "client could not pick a side: %d", m.side);
    OverlayScene s;
    memset(&s, 0, sizeof s);
    menu_draw(&m, &l, 0, &s);
    CHECK(s.text_count < OVERLAY_MAX_TEXTS && s.rect_count < OVERLAY_MAX_RECTS, "draw overflow %d %d", s.text_count, s.rect_count);
}

static void test_empty_name_keeps_the_old_one(void) {
    Menu m;
    Lobby l;
    MenuAction act;
    lobby_clear(&l);
    menu_init(&m, "Zed");
    m.open = true;
    menu_click(&m, &l, 500, 195, &act);
    for (int i = 0; i < 3; i++) menu_char(&m, 8, &act);
    menu_click(&m, &l, 300, 500, &act);
    CHECK(strcmp(m.name, "Zed") == 0, "empty box gave '%s'", m.name);
}

static void test_github_button(void) {
    Menu m;
    Lobby l;
    MenuAction act;
    lobby_clear(&l);
    menu_init(&m, "x");
    m.open = true;
    menu_click(&m, &l, 690, 618, &act);
    CHECK(act == ACTION_GITHUB, "lobby github action %d", act);
    m.room = ROOM_HOSTING;
    menu_click(&m, &l, 690, 618, &act);
    CHECK(act == ACTION_GITHUB, "room github action %d", act);
    menu_click(&m, &l, 600, 618, &act);
    CHECK(act == ACTION_LEAVE, "leave still works next to it: %d", act);
}

static void test_room_screen(void) {
    Menu m;
    Lobby l;
    MenuAction act;
    OverlayScene s;
    lobby_clear(&l);
    menu_init(&m, "x");
    m.open = true;
    m.room = ROOM_HOSTING;
    menu_click(&m, &l, 500, 250, &act);
    CHECK(act == ACTION_NONE, "host button works inside a room");
    menu_click(&m, &l, 500, 620, &act);
    CHECK(act == ACTION_LEAVE, "leave action %d", act);
    memset(&s, 0, sizeof s);
    menu_draw(&m, &l, 0, &s);
    CHECK(s.rect_count > 0 && s.rect_count <= OVERLAY_MAX_RECTS && s.text_count <= OVERLAY_MAX_TEXTS, "draw counts %d %d", s.rect_count, s.text_count);
    menu_escape(&m, &act);
    CHECK(!m.open, "escape did not close");
}

static void test_sweep_starts_near_and_covers_the_network(void) {
    uint32_t me = 0x0A5B04BBu, mask = 0xFFFFF000u; /* 10.91.4.187/20 */
    CHECK(lobby_sweep_address(me, mask, 0) == me, "the first probe is this machine");
    CHECK(lobby_sweep_address(me, mask, 1) == me + 1, "step 1");
    CHECK(lobby_sweep_address(me, mask, 2) == me - 1, "step 2");
    static unsigned char seen[4096];
    memset(seen, 0, sizeof seen);
    int found = 0, step;
    for (step = 0; ; step++) {
        uint32_t a = lobby_sweep_address(me, mask, step);
        if (!a) break;
        CHECK((a & mask) == (me & mask), "left the network at step %d", step);
        CHECK((a & ~mask) != 0 && (a & ~mask) != ~mask, "network or broadcast address at step %d", step);
        CHECK(!seen[a & ~mask], "repeat at step %d", step);
        seen[a & ~mask] = 1;
        if (a == 0x0A5B024Fu) found = step; /* 10.91.2.79, the friend in the report */
    }
    CHECK(step == 4094, "covered %d addresses", step);
    CHECK(found > 0 && found < 1300, "the friend took %d probes", found);

    uint32_t home = 0xC0A80105u; /* 192.168.1.5/24, near the bottom */
    memset(seen, 0, sizeof seen);
    for (step = 0; ; step++) {
        uint32_t a = lobby_sweep_address(home, 0xFFFFFF00u, step);
        if (!a) break;
        CHECK(!seen[a & 0xFF] && (a & 0xFF) != 0 && (a & 0xFF) != 255, "bad address at step %d", step);
        seen[a & 0xFF] = 1;
    }
    CHECK(step == 254, "covered %d of a /24", step);
    CHECK(lobby_sweep_address(home, 0, 1) == home + 1, "no mask should act like a /24");
    CHECK(lobby_sweep_address(home, 0, 254) == 0, "no mask should stop at a /24");
    CHECK(lobby_sweep_address(0x0A000005u, 0xFF000000u, LOBBY_SWEEP_MAX) == 0, "a huge network must be capped");
}

static void test_the_panel_shows_the_version(void) {
    Menu m;
    Lobby l;
    OverlayScene s;
    bool shown = false;
    menu_init(&m, "A");
    lobby_clear(&l);
    m.open = true;
    memset(&s, 0, sizeof s);
    menu_draw(&m, &l, 0, &s);
    for (int i = 0; i < s.text_count; i++) shown = shown || strcmp(s.texts[i].text, "V" MOD_VERSION) == 0;
    CHECK(shown, "no version on the panel");
}

int main(void) {
    test_sweep_starts_near_and_covers_the_network();
    test_the_panel_shows_the_version();
    test_rooms();
    test_names();
    test_panel_flow();
    test_join_paths();
    test_room_settings();
    test_github_button();
    test_empty_name_keeps_the_old_one();
    test_room_screen();
    printf("%d menu checks, %d failures\n", checks, failures);
    return failures != 0;
}
