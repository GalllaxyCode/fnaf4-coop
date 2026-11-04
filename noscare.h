/* The jumpscares setting: with it off, a death in the night shows only the game over screen. */
#ifndef NOSCARE_H
#define NOSCARE_H
#include <stdbool.h>

typedef void (*NoScareLog)(const char *fmt, ...);

/* On the game thread, every tick. Off hides the night's scare objects from the moment they are made,
 * and the game's own rules still end each scare and move on, so timing and co-op stay the same. */
void noscare_tick(bool off, NoScareLog log);
/* Frames whose jumpscare sounds the setting mutes. */
bool noscare_mutes(int frame);

#endif
