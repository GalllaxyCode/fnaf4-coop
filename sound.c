#include "sound.h"
#include "game.h"
#include "scarevol.h"
#include <string.h>

/* The exe loads MMFS2.dll late and calls it through its delay-load table (rva 0xAA140), by ordinal.
 * Read from the Steam exe (both editions share the code):
 *   ordinal 1077 is PlaySndW(manager, app, sample, window, flags, loops, channel), called only from the
 *     runtime's play routines at 0x42CF70 and 0x42D090. Speaker actions 11 and 12 pass the number in
 *     the action's sample parameter and the channel 1 to 32.
 *   ordinal 809 is SetSndChannelVolume(app, channel, volume), called only by speaker action 17 at
 *     0x409970. In MMFS2 it clamps to 0-100, stores the volume per channel and applies it to the sound
 *     playing there. */
#define RVA_SLOT_PLAY 0xB2760u
#define RVA_SLOT_CHANNEL_VOLUME 0xB27ACu
#define ORDINAL_PLAY 1077
#define ORDINAL_CHANNEL_VOLUME 809
#define HOOK_TRIES 200 /* ticks, about 10 s */
#define KNOWN_SAMPLES 256
#define NAME_LEN 32

typedef int(WINAPI *PlayFn)(void *manager, void *app, UINT sample, HWND window, DWORD flags, DWORD loops, UINT channel);
typedef void(WINAPI *VolumeFn)(void *app, UINT channel, int volume);

static BYTE *exe;
static SoundLog logf_;
static ScareVolume state;
static PlayFn real_play;
static VolumeFn real_volume;
static void *last_app;
static bool hooked, gave_up;
static int tries, logged_plays, traced_plays;
static bool trace; /* test mode: every play goes in the log */
static BYTE *known_for;
static BYTE *known[KNOWN_SAMPLES]; /* where each number's name was found in this frame's events */

bool sound_hooked(void) { return hooked; }

/* MMFS2 keeps each channel's volume at app + 28 * channel + 0x34, for channels up to app + 0x3C
 * (SetSndChannelVolume at 0x100120C0). Read back for the log only. */
static int stored_volume(void *app, UINT channel) {
    BYTE *a = app;
    if (!a || IsBadReadPtr(a, 0x40) || channel == 0 || channel > *(UINT *)(a + 0x3C) || IsBadReadPtr(a + 28 * channel + 0x34, 4)) return -1;
    return *(int *)(a + 28 * channel + 0x34);
}

/* By name, from the frame that is playing it. The numbers belong to the frame (sound 1 is crickets at
 * night and distortion in Nightmare's frame), and a frame can load where the last one was, so a
 * remembered place is read again every time and only trusted if it still holds this number. */
static bool is_scare(UINT sample, char *name) {
    BYTE *events = game_events_now();
    if (events != known_for) {
        memset(known, 0, sizeof known);
        known_for = events;
    }
    BYTE **slot = sample < KNOWN_SAMPLES ? &known[sample] : NULL;
    if (!slot || !game_sample_name_at(*slot, sample, name, NAME_LEN)) {
        BYTE *param = game_sample_param(sample);
        if (slot) *slot = param;
        if (!game_sample_name_at(param, sample, name, NAME_LEN)) return false;
    }
    return scarevol_is_scare(name);
}

static int WINAPI hook_play(void *manager, void *app, UINT sample, HWND window, DWORD flags, DWORD loops, UINT channel) {
    int set, result = 0;
    char name[NAME_LEN];
    last_app = app;
    bool scare = is_scare(sample, name);
    bool play = scarevol_play(&state, (int)channel, scare, &set);
    if (play) {
        if (set >= 0) real_volume(app, channel, set);
        result = real_play(manager, app, sample, window, flags, loops, channel);
        /* Set again so the sound that now holds the channel gets it, whatever the play did. */
        if (set >= 0) real_volume(app, channel, set);
    }
    if (trace && traced_plays < 300) {
        traced_plays++;
        if (logf_) logf_("sound %u '%s' on channel %u flags %lx loops %lu", sample, name, channel, flags, loops);
    }
    if (scare && logged_plays < 40) {
        logged_plays++;
        if (!logf_) return result;
        if (play) logf_("jumpscare sound %s on channel %u played, setting %d, channel volume %d", name[0] ? name : "(known)", channel, state.volume, stored_volume(app, channel));
        else logf_("jumpscare sound %s on channel %u silenced, setting 0", name[0] ? name : "(known)", channel);
    }
    return result;
}

static void WINAPI hook_volume(void *app, UINT channel, int volume) {
    last_app = app;
    int heard = scarevol_channel_volume(&state, (int)channel, volume);
    real_volume(app, channel, heard);
    if (heard != volume && logged_plays < 40 && logf_) {
        logged_plays++;
        logf_("jumpscare channel %u: the game set %d, now %d", channel, volume, stored_volume(app, channel));
    }
}

void sound_trace(bool on) { trace = on; }

void sound_init(BYTE *exe_base, int volume, SoundLog log) {
    exe = exe_base;
    logf_ = log;
    scarevol_init(&state, volume);
}

static bool inside(const void *p, const BYTE *module) {
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(module + ((const IMAGE_DOS_HEADER *)module)->e_lfanew);
    return (const BYTE *)p >= module && (const BYTE *)p < module + nt->OptionalHeader.SizeOfImage;
}

/* A slot holds the delay-load stub inside the exe until first used, then the real function. Anything
 * else means a build this code wasn't read from, and it stays untouched. */
static bool slot_ok(void **slot, void *real) { return *slot == real || inside(*slot, exe); }

static void write_slot(void **slot, void *value) {
    DWORD old;
    if (!VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old)) return;
    *slot = value;
    VirtualProtect(slot, sizeof *slot, old, &old);
}

static void install(void) {
    HMODULE mmfs2 = GetModuleHandleA("mmfs2.dll");
    if (!mmfs2) {
        if (++tries == HOOK_TRIES) {
            gave_up = true;
            if (logf_) logf_("jumpscare volume off: MMFS2.dll never loaded");
        }
        return;
    }
    void **play_slot = (void **)(exe + RVA_SLOT_PLAY), **volume_slot = (void **)(exe + RVA_SLOT_CHANNEL_VOLUME);
    void *play = (void *)GetProcAddress(mmfs2, MAKEINTRESOURCEA(ORDINAL_PLAY));
    void *volume = (void *)GetProcAddress(mmfs2, MAKEINTRESOURCEA(ORDINAL_CHANNEL_VOLUME));
    gave_up = true;
    if (!play || !volume || IsBadReadPtr(play_slot, 4) || IsBadReadPtr(volume_slot, 4) || !slot_ok(play_slot, play) || !slot_ok(volume_slot, volume)) {
        if (logf_) logf_("jumpscare volume off: the sound calls aren't where this build has them");
        return;
    }
    real_play = (PlayFn)play;
    real_volume = (VolumeFn)volume;
    write_slot(play_slot, (void *)hook_play);
    write_slot(volume_slot, (void *)hook_volume);
    hooked = *play_slot == (void *)hook_play && *volume_slot == (void *)hook_volume;
    if (logf_) logf_("jumpscare volume %s, at %d", hooked ? "on" : "failed", state.volume);
}

void sound_tick(int volume) {
    if (!hooked && !gave_up) install();
    volume = scarevol_clamp(volume);
    if (volume == state.volume) return;
    state.volume = volume;
    if (!hooked || !last_app) return;
    for (int ch = 1; ch <= SCAREVOL_CHANNELS; ch++) {
        int set = scarevol_retune(&state, ch);
        if (set >= 0) real_volume(last_app, (UINT)ch, set);
    }
}
