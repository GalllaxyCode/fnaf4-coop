/* Jumpscare volume in the game itself: hooks the two calls the runtime makes into MMFS2.dll to play a
 * sound on a channel and to set a channel's volume. */
#ifndef SOUND_H
#define SOUND_H
#include <stdbool.h>
#include <windows.h>

typedef void (*SoundLog)(const char *fmt, ...);

void sound_init(BYTE *exe_base, int volume, SoundLog log);
/* On the game thread, every tick: hooks the calls once MMFS2.dll is loaded, and applies the setting. */
void sound_tick(int volume);
bool sound_hooked(void);
void sound_trace(bool on); /* test mode: logs every sound played */

#endif
