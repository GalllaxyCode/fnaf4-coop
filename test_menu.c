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
    menu_click(&m, &l, 400, 618, &act);
    CHECK(act == ACTION_LEAVE, "leave still works next to it: %d", act);
}

static bool shows(const OverlayScene *s, const char *text) {
    for (int i = 0; i < s->text_count; i++)
        if (strcmp(s->texts[i].text, text) == 0) return true;
    return false;
}

static void test_settings_page(void) {
    Menu m;
    Lobby l;
    MenuAction act;
    OverlayScene s;
    lobby_clear(&l);
    menu_init(&m, "x");
    CHECK(m.jumpscare_volume == 100, "starts at %d", m.jumpscare_volume);
    menu_click(&m, &l, 500, 650, &act);
    menu_click(&m, &l, 576, 618, &act);
    CHECK(m.settings_open && act == ACTION_NONE, "settings button did not open the page");
    memset(&s, 0, sizeof s);
    menu_draw(&m, &l, 0, &s);
    CHECK(shows(&s, "JUMPSCARE VOLUME") && shows(&s, "100") && shows(&s, "BACK") && !shows(&s, "HOST A ROOM"), "page contents");
    CHECK(s.text_count < OVERLAY_MAX_TEXTS && s.rect_count < OVERLAY_MAX_RECTS, "draw overflow %d %d", s.text_count, s.rect_count);

    menu_click(&m, &l, 288, 272, &act);
    CHECK(m.jumpscare_volume == 0 && m.dragging, "left end gave %d", m.jumpscare_volume);
    menu_mouse(&m, 464, 272);
    CHECK(m.jumpscare_volume == 50, "middle gave %d", m.jumpscare_volume);
    menu_mouse(&m, 2000, 900);
    CHECK(m.jumpscare_volume == 100, "past the end gave %d", m.jumpscare_volume);
    menu_mouse(&m, -50, 272);
    CHECK(m.jumpscare_volume == 0, "before the start gave %d", m.jumpscare_volume);
    memset(&s, 0, sizeof s);
    menu_draw(&m, &l, 0, &s);
    CHECK(shows(&s, "OFF"), "0 not shown as OFF");
    menu_release(&m, &act);
    CHECK(act == ACTION_SETTINGS_CHANGED && !m.dragging, "release action %d", act);
    menu_release(&m, &act);
    CHECK(act == ACTION_NONE, "a second release saved again");
    menu_mouse(&m, 464, 272);
    CHECK(m.jumpscare_volume == 0, "hovering moved the slider");

    Announce a = announce(1, "Alice");
    lobby_note(&l, &a, 1, 0);
    menu_click(&m, &l, 500, 340, &act);
    CHECK(act == ACTION_NONE && !m.dragging, "a room row worked under the page: %d", act);
    menu_click(&m, &l, 500, 195, &act);
    CHECK(m.focus == FOCUS_NONE, "name field focused under the page");
    menu_click(&m, &l, 576, 618, &act);
    CHECK(!m.settings_open && m.open, "back did not return");
    menu_click(&m, &l, 576, 618, &act);
    menu_escape(&m, &act);
    CHECK(!m.settings_open && m.open, "escape closed the panel, not the page");
    menu_escape(&m, &act);
    CHECK(!m.open, "second escape did not close");

    menu_click(&m, &l, 500, 650, &act);
    CHECK(m.open && !m.settings_open, "the panel reopened on the settings page");
    m.room = ROOM_HOSTING;
    menu_click(&m, &l, 576, 618, &act);
    menu_click(&m, &l, 400, 618, &act);
    CHECK(act == ACTION_NONE && m.settings_open, "leave worked under the page: %d", act);
    menu_click(&m, &l, 600, 272, &act);
    menu_escape(&m, &act);
    CHECK(act == ACTION_SETTINGS_CHANGED && !m.dragging, "escape mid-drag lost the change: %d", act);
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

static void test_steam_rooms_sit_beside_wifi_rooms(void) {
    Lobby l;
    Announce a;
    lobby_clear(&l);
    memset(&a, 0, sizeof a);
    a.magic = LOBBY_MAGIC;
    a.version = SYNC_VERSION;
    a.session = 7;
    strcpy(a.name, "LAN");
    CHECK(lobby_note(&l, &a, 10, 0), "wifi room");
    CHECK(lobby_note_steam(&l, 0x0186000000000001ULL, "Friend", 0), "steam room");
    CHECK(lobby_note_steam(&l, 0x0186000000000001ULL, "Friend", 500) && lobby_count(&l) == 2, "steam room twice");
    CHECK(!lobby_note_steam(&l, 0, "Nobody", 0), "a room with no lobby");
    const Room *steam = NULL, *lan = NULL;
    for (int i = 0; i < lobby_count(&l); i++) {
        if (lobby_room(&l, i)->steam_lobby) steam = lobby_room(&l, i);
        else lan = lobby_room(&l, i);
    }
    CHECK(steam && strcmp(steam->name, "Friend") == 0 && steam->address == 0, "steam room fields");
    CHECK(lan && lan->steam_lobby == 0 && lan->address == 10, "wifi room fields");
    lobby_expire(&l, 4000);
    CHECK(lobby_count(&l) == 0, "rooms never expired");
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

static bool has_text(const OverlayScene *s, const char *want) {
    for (int i = 0; i < s->text_count; i++)
        if (strstr(s->texts[i].text, want)) return true;
    return false;
}

/* The Steam overlay doesn't open in FNAF 4, so the host invites from the panel. */
static void test_invite_page(void) {
    Menu m;
    Lobby l;
    MenuAction act;
    OverlayScene s;
    lobby_clear(&l);
    menu_init(&m, "Host");
    m.open = true;
    m.room = ROOM_HOSTING;
    memset(&s, 0, sizeof s);
    menu_draw(&m, &l, 0, &s);
    CHECK(!has_text(&s, "INVITE STEAM FRIENDS"), "invite button without a Steam room");
    menu_click(&m, &l, 500, 530, &act);
    CHECK(act == ACTION_NONE && !m.invite_open, "invite page opened without a Steam room");

    m.steam = m.steam_open = true;
    strcpy(m.lobby_id, "109775241234567890");
    memset(&s, 0, sizeof s);
    menu_draw(&m, &l, 0, &s);
    CHECK(has_text(&s, "INVITE STEAM FRIENDS"), "no invite button on a Steam room");
    menu_click(&m, &l, 500, 530, &act);
    CHECK(act == ACTION_INVITE_OPENED && m.invite_open, "invite button gave %d", act);

    memset(&s, 0, sizeof s);
    menu_draw(&m, &l, 0, &s);
    CHECK(has_text(&s, "No Steam friends online"), "empty list not explained");
    CHECK(has_text(&s, "109775241234567890"), "lobby id not shown");
    CHECK(has_text(&s, "BACK"), "no way back");

    m.friend_count = 2;
    strcpy(m.friends[0], "Alice");
    strcpy(m.friends[1], "Bob");
    menu_click(&m, &l, 500, 206 + 50 + 20, &act);
    CHECK(act == ACTION_INVITE && m.invite_index == 1, "second friend gave %d/%d", act, m.invite_index);
    m.invited[1] = true;
    menu_click(&m, &l, 500, 206 + 50 + 20, &act);
    CHECK(act == ACTION_NONE, "a friend was invited twice");
    menu_click(&m, &l, 500, 206 + 3 * 50 + 20, &act);
    CHECK(act == ACTION_NONE && m.invite_open, "an empty row did something");
    memset(&s, 0, sizeof s);
    menu_draw(&m, &l, 0, &s);
    CHECK(has_text(&s, "Alice - INVITE") && has_text(&s, "Bob - INVITED"), "rows not labelled");
    CHECK(s.rect_count <= OVERLAY_MAX_RECTS && s.text_count <= OVERLAY_MAX_TEXTS, "draw counts %d %d", s.rect_count, s.text_count);

    menu_click(&m, &l, 560, 618, &act); /* BACK, where SETTINGS sits */
    CHECK(!m.invite_open && !m.settings_open, "back went to %d %d", m.invite_open, m.settings_open);
    menu_click(&m, &l, 500, 530, &act);
    menu_escape(&m, &act);
    CHECK(!m.invite_open && m.open, "escape should leave the invite page first");

    menu_click(&m, &l, 500, 530, &act);
    m.room = ROOM_JOINED;
    menu_click(&m, &l, 500, 300, &act);
    CHECK(!m.invite_open, "a client kept the host's invite page");
}

static void test_steam_note_shows(void) {
    Menu m;
    Lobby l;
    OverlayScene s;
    lobby_clear(&l);
    menu_init(&m, "A");
    m.open = true;
    strcpy(m.steam_note, "STEAM OFF: NO STEAM_API.DLL");
    memset(&s, 0, sizeof s);
    menu_draw(&m, &l, 0, &s);
    CHECK(has_text(&s, "NO STEAM_API.DLL"), "the reason Steam is off isn't shown");
}

int main(void) {
    test_invite_page();
    test_steam_note_shows();
    test_sweep_starts_near_and_covers_the_network();
    test_the_panel_shows_the_version();
    test_steam_rooms_sit_beside_wifi_rooms();
    test_rooms();
    test_names();
    test_panel_flow();
    test_join_paths();
    test_room_settings();
    test_github_button();
    test_settings_page();
    test_empty_name_keeps_the_old_one();
    test_room_screen();
    printf("%d menu checks, %d failures\n", checks, failures);
    return failures != 0;
}
