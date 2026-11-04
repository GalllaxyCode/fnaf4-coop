/* The CO-OP button and panel on the title screen. Works in the game's 1024x768 view coordinates and
 * has no window or network access, so it can be tested on its own. */
#ifndef MENU_H
#define MENU_H
#include <stdbool.h>
#include "lobby.h"
#include "overlay.h"

#define MENU_PLAYERS_LEN 60
#define MENU_STATUS_LEN 60
#define MENU_MAX_FRIENDS 7

typedef enum { ROOM_NONE, ROOM_HOSTING, ROOM_JOINING, ROOM_JOINED } RoomState;
typedef enum { ACTION_NONE, ACTION_HOST, ACTION_JOIN_ROOM, ACTION_JOIN_ADDRESS, ACTION_LEAVE, ACTION_NAME_CHANGED, ACTION_GITHUB, ACTION_SETTINGS_CHANGED, ACTION_INVITE_OPENED, ACTION_INVITE } MenuAction;
typedef enum { FOCUS_NONE, FOCUS_NAME, FOCUS_ADDRESS } MenuFocus;

typedef struct {
    bool open;
    MenuFocus focus;
    int hover_x, hover_y;
    char name[SYNC_NAME_LEN];
    char last_name[SYNC_NAME_LEN]; /* put back if the box is left empty */
    char address[LOBBY_ADDRESS_LEN];
    char status[MENU_STATUS_LEN];
    RoomState room;
    char room_name[SYNC_NAME_LEN];
    char my_address[LOBBY_ADDRESS_LEN];
    char players[MENU_PLAYERS_LEN];
    int player_count;
    int join_index; /* set with ACTION_JOIN_ROOM */
    int mode;
    bool last_standing;
    int side;
    bool steam; /* Steam is running, so rooms of Steam friends show too */
    bool steam_open; /* this host's room is also open on Steam */
    char steam_note[MENU_STATUS_LEN]; /* STEAM ON, or why it's off */
    char lobby_id[24]; /* the host's Steam lobby, for friends to type */
    bool invite_open; /* the host's page of Steam friends to invite */
    int friend_count;
    char friends[MENU_MAX_FRIENDS][SYNC_NAME_LEN];
    bool invited[MENU_MAX_FRIENDS];
    int invite_index; /* set with ACTION_INVITE */
    bool settings_open; /* the settings page is showing instead of the rooms */
    bool dragging; /* the jumpscare volume slider is held */
    int jumpscare_volume; /* 0 to 100, this player's own */
    bool jumpscares_off; /* deaths show only the game over screen, this player's own */
} Menu;

void menu_init(Menu *m, const char *name);
void menu_draw(const Menu *m, const Lobby *lobby, uint32_t now_ms, OverlayScene *scene);
void menu_mouse(Menu *m, int x, int y);
/* The mouse button went up. Ends a slider drag with ACTION_SETTINGS_CHANGED. */
void menu_release(Menu *m, MenuAction *action);
/* Returns true when the click belongs to the menu, not the game. */
bool menu_click(Menu *m, const Lobby *lobby, int x, int y, MenuAction *action);
void menu_char(Menu *m, int c, MenuAction *action);
void menu_escape(Menu *m, MenuAction *action);
void menu_set_status(Menu *m, const char *text);
#define MOD_VERSION "0.5.5"

/* Mode names and side names, also used by the in-game display. */
const char *menu_mode_name(int mode);
const char *menu_side_name(int mode, int side);
int menu_side_count(int mode);

#endif
