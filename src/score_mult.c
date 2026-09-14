#include "score_mult.h"
#include "cheats.h"
#include "native_cheats.h"
#include "utils.h"

/* ================================================================
 * Score Multiplier Module
 *
 * Lifecycle wrapper around native_score_multiplier() that handles
 * transitions between multiplier values.
 *
 * g_cheats.score_mult is the user-set value (0 = off).
 * g_prev_mult tracks what is currently applied, so we only call
 * the native system when the value actually changes.
 * ================================================================ */

/* Previously-applied multiplier (0 = none applied) */
static int g_prev_mult = 0;

/* ------------------------------------------------------------------ */

void score_mult_set(int multiplier) {
    if (multiplier != 0 && multiplier != 2 && multiplier != 4 &&
        multiplier != 6 && multiplier != 8 && multiplier != 10) {
        LOG("Score Multiplier: ignored invalid value %d (valid: 0,2,4,6,8,10)", multiplier);
        return;
    }
    g_cheats.score_mult = multiplier;
}

/* ------------------------------------------------------------------ */

void score_mult_apply(void) {
    int val = g_cheats.score_mult;

    /* No change since last apply — nothing to do */
    if (val == g_prev_mult)
        return;

    /* Zero means "off" */
    if (val <= 0) {
        score_mult_remove();
        return;
    }

    /* Switching from one multiplier to another:
     *   native_score_multiplier(0) disables ALL score cheats
     *   (including always_score_multiply), giving us a clean slate. */
    if (g_prev_mult > 0 && !g_cheats.always_gold) {
        native_score_multiplier(0);
    }

    /* Activate the new multiplier cheat */
    native_score_multiplier(val);

    /* Also enable always_score_multiply so the multiplier applies to
     * all stud pickups, not just combo-chain studs. */
    native_always_score_multiply(1);

    g_prev_mult = val;

    LOG("Score Multiplier: x%d", val);
}

/* ------------------------------------------------------------------ */

void score_mult_remove(void) {
    if (g_prev_mult == 0)
        return;

    /* native_score_multiplier(0) disables all score cheats + always multiply */
    native_score_multiplier(0);

    g_prev_mult = 0;
    LOG("Score Multiplier: off");
}

/* ------------------------------------------------------------------ */

int score_mult_get(void) {
    return g_cheats.score_mult;
}
