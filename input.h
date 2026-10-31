/* Key state as the game sees it, so one player's input can act on another player's copy. */
#ifndef INPUT_H
#define INPUT_H
#include <stdbool.h>

void input_install(void);
bool input_hooked(void);
bool key_real(int vk);
void key_force(int vk, bool down); /* what the game sees on top of the real keyboard */
void key_bot(int vk, bool down); /* test input, treated as the player's own */
void key_mute(bool muted); /* the game sees no keys while the co-op panel has them */

#endif
