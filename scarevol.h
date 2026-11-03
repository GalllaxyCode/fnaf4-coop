/* The jumpscare volume setting: which sounds are jumpscares, and what each sound channel's volume
 * becomes while one plays. No game or window access, so it can be tested on its own. */
#ifndef SCAREVOL_H
#define SCAREVOL_H
#include <stdbool.h>

#define SCAREVOL_CHANNELS 32 /* the game's channels are 1 to 32 */
#define SCAREVOL_MAX 100

typedef struct {
    int volume;                            /* the setting, 0 to 100 */
    int wanted[SCAREVOL_CHANNELS + 1];     /* each channel's volume as the game set it, 0 to 100 */
    bool scare[SCAREVOL_CHANNELS + 1];     /* the channel's last sound was a jumpscare */
} ScareVolume;

void scarevol_init(ScareVolume *s, int volume);
int scarevol_clamp(int volume);
/* The jumpscare sounds, by name, the same in both editions: scream2 (every night kill and five in the
 * Extras), screamshort (Foxy in the closet, the cutscenes), scream3 (Plushtrap, BB, two in the Extras)
 * and distortion (Nightmare). */
bool scarevol_is_scare(const char *name);
/* The game sets a channel's volume. Returns the volume to really set. */
int scarevol_channel_volume(ScareVolume *s, int channel, int volume);
/* The game plays a sample on a channel. Returns false if it must not play at all. *set is the
 * channel volume to set around the play, or -1 to leave it alone. */
bool scarevol_play(ScareVolume *s, int channel, bool scare, int *set);
/* The setting changed. Returns the volume a channel must now have, or -1 to leave it alone. */
int scarevol_retune(const ScareVolume *s, int channel);

#endif
