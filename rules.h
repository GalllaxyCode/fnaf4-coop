/* What each player's copy of the game does in a shared night. */
#ifndef RULES_H
#define RULES_H
#include <stdbool.h>
#include "overlay.h"
#include "sync.h"

void rules_init(bool is_host);
void rules_setup(int mode, int side); /* before each tick */
bool rules_host_resume(Local *l, uint32_t now); /* a dead host reloads the night it is still running */
bool rules_map_open(void);
bool rules_may_follow(int frame); /* a client may go to the host's frame */
void rules_map_click(int x, int y, uint32_t now);
void rules_command(int zone);
void rules_frame(void); /* keeps the frame's event patches applied */
void rules_read(Local *l);
void rules_apply(const Output *o, const Local *l, uint32_t now);
void rules_force_kill(int kind);
void rules_scene(const Output *o, const Local *l, uint32_t now, OverlayScene *scene);
bool rules_host_off_is_active(void);
const char *rules_debug(void);

#endif
