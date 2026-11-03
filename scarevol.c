#include "scarevol.h"
#include <string.h>

/* By name: the numbers the runtime plays sounds by are its own, not the bank's. Seen live: crickets 1,
 * title 2, distortion 3, breathing1 23, call3 45, against bank handles 2, 43, 65, 23 and 70. */
static const char *const scare_names[] = {"scream2", "screamshort", "scream3", "distortion"};
#define DEFAULT_CHANNEL_VOLUME 100

static bool valid(int channel) { return channel >= 1 && channel <= SCAREVOL_CHANNELS; }

int scarevol_clamp(int volume) { return volume < 0 ? 0 : volume > SCAREVOL_MAX ? SCAREVOL_MAX : volume; }

void scarevol_init(ScareVolume *s, int volume) {
    s->volume = scarevol_clamp(volume);
    for (int i = 0; i <= SCAREVOL_CHANNELS; i++) {
        s->wanted[i] = DEFAULT_CHANNEL_VOLUME;
        s->scare[i] = false;
    }
}

bool scarevol_is_scare(const char *name) {
    for (size_t i = 0; name && i < sizeof scare_names / sizeof scare_names[0]; i++)
        if (strcmp(name, scare_names[i]) == 0) return true;
    return false;
}

static int heard(const ScareVolume *s, int channel) {
    int wanted = s->wanted[channel];
    return s->scare[channel] ? wanted * s->volume / SCAREVOL_MAX : wanted;
}

int scarevol_channel_volume(ScareVolume *s, int channel, int volume) {
    if (!valid(channel)) return volume;
    s->wanted[channel] = scarevol_clamp(volume);
    return heard(s, channel);
}

bool scarevol_play(ScareVolume *s, int channel, bool scare, int *set) {
    *set = -1;
    if (scare && s->volume == 0) return false;
    if (!valid(channel)) return true;
    /* The game often sets the volume after the play, so a scare channel is turned down as soon as it
     * holds a scare, and back up once it holds anything else. */
    if (scare || s->scare[channel]) {
        s->scare[channel] = scare;
        *set = heard(s, channel);
    }
    return true;
}

int scarevol_retune(const ScareVolume *s, int channel) {
    if (!valid(channel) || !s->scare[channel]) return -1;
    return heard(s, channel);
}
