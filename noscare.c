/* Every night death creates one of these objects, plays a scream, and when the object's animation ends
 * a rule goes to the game over frame (rules 203-206, 288, 300, 301, 370). Foxy's closet bite (254) is a
 * scare without a death, and rule 255 removes it. Only frame 3 uses these objects. */
#include "noscare.h"
#include "game.h"

#define FRAME_GAME_OVER 4
#define RULE_SHOW_SCARES 224 /* "always": shows the jumpscare object, among others */
#define RULE_NIGHTMARE_FRAME 431 /* Nightmare's kill leaves for his own scare frame */
#define ACT_HIDE 26
#define ACT_SHOW 27
#define SYS_STORYBOARD -3
#define ACT_GOTO_FRAME 2
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static const wchar_t *const scare_objects[] = {L"jumpscare", L"foxy bite", L"freddy bedroom attack", L"cupcake attack", L"bonnie bedroom attack", L"fredbear scare"};
static int original[COUNT(scare_objects)] = {-1, -1, -1, -1, -1, -1}; /* visible at start, as the game has it */
static bool held_nightmare, warned, seen[COUNT(scare_objects)];
static int logged = -1;

bool noscare_mutes(int frame) { return frame == FRAME_LEVEL || frame == FRAME_GAME_OVER; }

void noscare_tick(bool off, NoScareLog log) {
    if (game_frame() != FRAME_LEVEL) return;
    int changed = 0, found = 0;
    for (size_t i = 0; i < COUNT(scare_objects); i++) {
        WORD *flags = game_oc_flags2(scare_objects[i]);
        if (!flags) continue;
        found++;
        if (original[i] < 0) original[i] = *flags & OCFLAGS2_VISIBLEATSTART;
        WORD want = off ? (WORD)(*flags & ~OCFLAGS2_VISIBLEATSTART) : (WORD)((*flags & ~OCFLAGS2_VISIBLEATSTART) | original[i]);
        if (*flags != want) changed++;
        *flags = want;
    }
    /* Each scare once in the log, so a player's report says whether it was hidden. */
    for (size_t i = 0; i < COUNT(scare_objects); i++) {
        int anim, dir, frame;
        bool hidden;
        bool here = anim_read(obj(scare_objects[i]), &anim, &dir, &frame, &hidden);
        if (here && !seen[i]) log("jumpscares: %ls appeared, %s", scare_objects[i], hidden ? "hidden" : "shown");
        seen[i] = here;
    }
    /* Rule 224 shows the jumpscare object every frame, so with jumpscares off it hides it instead. */
    BYTE *show = evg(RULE_SHOW_SCARES);
    short *action = act_number(show, L"jumpscare", off ? ACT_SHOW : ACT_HIDE);
    if (action) {
        *action = off ? ACT_HIDE : ACT_SHOW;
        changed++;
    }
    /* Without his frame, Nightmare's kill ends through rule 370 like Fredbear's, at game over. */
    BYTE *nightmare = evg(RULE_NIGHTMARE_FRAME);
    if (act_is(act_at(nightmare, 0), SYS_STORYBOARD, ACT_GOTO_FRAME)) {
        if (off && !evg_is_disabled(RULE_NIGHTMARE_FRAME)) {
            evg_disable(RULE_NIGHTMARE_FRAME);
            held_nightmare = true;
            changed++;
        } else if (!off && held_nightmare) {
            evg_enable(RULE_NIGHTMARE_FRAME);
            held_nightmare = false;
            changed++;
        }
    } else if (!warned && evg_count() > RULE_NIGHTMARE_FRAME) {
        warned = true;
        log("jumpscares: rule %d isn't Nightmare's frame change here, left alone", RULE_NIGHTMARE_FRAME);
    }
    if (changed && (int)off != logged) log("jumpscares %s: %d of %zu scare objects, rule %d %s", off ? "off" : "on", found, COUNT(scare_objects), RULE_SHOW_SCARES, show ? "found" : "missing");
    if (changed) logged = off;
}
