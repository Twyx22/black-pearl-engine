#include "stud_magnet.h"
#include "utils.h"
#include "native_cheats.h"

/* ================================================================
 * Stud Magnet — Activate game's native cheat_stud_magnet
 *
 * Uses the native cheat activation system to call the game's own
 * stud attraction mechanic.  No binary patches needed — the game
 * already has a working stud magnet, it just needs to be flagged
 * as enabled.
 * ================================================================ */

void stud_magnet_apply(void) {
    native_stud_magnet(1);
    LOG("Stud Magnet: enabled via native cheat system");
}

void stud_magnet_remove(void) {
    native_stud_magnet(0);
    LOG("Stud Magnet: disabled");
}
