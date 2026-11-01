/* Key state as the game sees it, so one player's input can act on another player's copy. */
#ifndef INPUT_H
#define INPUT_H
#include <stdbool.h>

void input_install(void);
bool input_hooked(void);
bool key_real(int vk);
void key_block(int vk, bool blocked); /* the player's own key stops working */
bool key_usable(int vk); /* held, and not blocked */
void key_force(int vk, bool down); /* what the game sees on top of the real keyboard */
void key_bot(int vk, bool down); /* test input, treated as the player's own */
void cursor_set(int x, int y); /* test only: this copy gets its own cursor */
void cursor_freeze(bool frozen); /* a copy in the background keeps its last cursor position */
void key_mute(bool muted); /* the game sees no keys while the co-op panel has them */

#endif
