/* Tests for the jumpscare volume rules, with the game's own call orders from its event sheets. */
#include "scarevol.h"
#include <stdio.h>

static int failures, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); puts(""); } } while (0)

#define SCARE true
#define PLAIN false

static void test_which_sounds_are_scares(void) {
    CHECK(scarevol_is_scare("scream2") && scarevol_is_scare("screamshort") && scarevol_is_scare("scream3") && scarevol_is_scare("distortion"), "a scare sound missed");
    CHECK(!scarevol_is_scare("crickets") && !scarevol_is_scare("select3") && !scarevol_is_scare("breathing1") && !scarevol_is_scare("fredbear"), "an ordinary sound counted");
    CHECK(!scarevol_is_scare("scream") && !scarevol_is_scare("scream22") && !scarevol_is_scare("") && !scarevol_is_scare(NULL), "a near name counted");
}

/* Level rule 202: play scream2 on 16, then set channel 16 to 100. */
static void test_night_kill_play_then_volume(void) {
    ScareVolume s;
    int set;
    scarevol_init(&s, 40);
    CHECK(scarevol_play(&s, 16, SCARE, &set) && set == 40, "scream2 at 40 set %d", set);
    CHECK(scarevol_channel_volume(&s, 16, 100) == 40, "the rule's own 100 wasn't scaled");
    CHECK(scarevol_retune(&s, 16) == 40, "retune %d", scarevol_retune(&s, 16));
}

/* Plushtrap rule 45: set channel 4 to 100, then play scream3 on 4. */
static void test_minigame_volume_then_play(void) {
    ScareVolume s;
    int set;
    scarevol_init(&s, 25);
    CHECK(scarevol_channel_volume(&s, 4, 100) == 100, "a plain channel was scaled");
    CHECK(scarevol_play(&s, 4, SCARE, &set) && set == 25, "scream3 set %d", set);
}

/* Extras: channel 3 plays the menu blip and the gallery's screams, and nothing sets its volume. */
static void test_shared_channel_comes_back(void) {
    ScareVolume s;
    int set;
    scarevol_init(&s, 30);
    CHECK(scarevol_play(&s, 3, PLAIN, &set) && set == -1, "a blip touched the volume: %d", set);
    CHECK(scarevol_play(&s, 3, SCARE, &set) && set == 30, "gallery scream set %d", set);
    CHECK(scarevol_play(&s, 3, PLAIN, &set) && set == 100, "the blip after it stayed at %d", set);
    CHECK(scarevol_play(&s, 3, PLAIN, &set) && set == -1, "set again for no reason: %d", set);
    scarevol_channel_volume(&s, 3, 60);
    CHECK(scarevol_play(&s, 3, SCARE, &set) && set == 18, "60%% of 30 gave %d", set);
    CHECK(scarevol_play(&s, 3, PLAIN, &set) && set == 60, "the game's 60 came back as %d", set);
}

static void test_zero_never_plays_a_scare(void) {
    ScareVolume s;
    int set;
    scarevol_init(&s, 0);
    CHECK(!scarevol_play(&s, 16, SCARE, &set), "scream2 played at 0");
    CHECK(!scarevol_play(&s, 1, SCARE, &set), "Nightmare played at 0");
    CHECK(!scarevol_play(&s, -1, SCARE, &set), "a scare on no channel played at 0");
    CHECK(scarevol_play(&s, 2, PLAIN, &set) && set == -1, "an ordinary sound was stopped");
    CHECK(scarevol_channel_volume(&s, 16, 100) == 100, "a channel that never played a scare was scaled");
}

static void test_full_volume_changes_nothing(void) {
    ScareVolume s;
    int set;
    scarevol_init(&s, 100);
    CHECK(scarevol_play(&s, 16, SCARE, &set) && set == 100, "full volume set %d", set);
    CHECK(scarevol_channel_volume(&s, 16, 100) == 100 && scarevol_channel_volume(&s, 16, 70) == 70, "full volume changed the game's own");
}

static void test_setting_changes_while_playing(void) {
    ScareVolume s;
    int set;
    scarevol_init(&s, 80);
    scarevol_play(&s, 1, SCARE, &set);
    scarevol_play(&s, 2, PLAIN, &set);
    s.volume = 20;
    CHECK(scarevol_retune(&s, 1) == 20, "scare channel retuned to %d", scarevol_retune(&s, 1));
    CHECK(scarevol_retune(&s, 2) == -1, "ordinary channel retuned");
    CHECK(scarevol_retune(&s, 0) == -1 && scarevol_retune(&s, 33) == -1, "out of range channel retuned");
}

static void test_bounds(void) {
    ScareVolume s;
    int set;
    scarevol_init(&s, 250);
    CHECK(s.volume == 100, "init kept %d", s.volume);
    scarevol_init(&s, -5);
    CHECK(s.volume == 0, "init kept %d", s.volume);
    scarevol_init(&s, 50);
    CHECK(scarevol_channel_volume(&s, 40, 77) == 77 && scarevol_channel_volume(&s, 0, 77) == 77, "out of range channel changed");
    CHECK(scarevol_play(&s, 40, SCARE, &set) && set == -1, "out of range channel set %d", set);
    scarevol_play(&s, 5, SCARE, &set);
    CHECK(scarevol_channel_volume(&s, 5, 300) == 50 && scarevol_channel_volume(&s, 5, -9) == 0, "the game's volume wasn't clamped");
}

int main(void) {
    test_which_sounds_are_scares();
    test_night_kill_play_then_volume();
    test_minigame_volume_then_play();
    test_shared_channel_comes_back();
    test_zero_never_plays_a_scare();
    test_full_volume_changes_nothing();
    test_setting_changes_while_playing();
    test_bounds();
    printf("%d jumpscare volume checks, %d failures\n", checks, failures);
    return failures != 0;
}
