#include "always_gold.h"
#include "cheats.h"
#include "utils.h"
#include "native_cheats.h"

/* ================================================================
 * Always Gold (T3.10)
 *
 * Simply toggles the native "always score multiply" cheat.
 * When enabled, all stud pickups are multiplied (default 10x).
 * ================================================================ */

static int g_always_gold_enabled = 0;

void always_gold_apply(void)
{
    if (g_always_gold_enabled) return;
    native_always_score_multiply(1);
    g_always_gold_enabled = 1;
    LOG("Always Gold: activated (native cheat: always_score_multiply)");
}

void always_gold_remove(void)
{
    if (!g_always_gold_enabled) return;
    g_always_gold_enabled = 0;
    /* score_mult shares always_score_multiply — don't cut it while in use */
    if (!g_cheats.score_mult)
        native_always_score_multiply(0);
    LOG("Always Gold: deactivated");
}

int always_gold_get_enabled(void)
{
    return g_always_gold_enabled;
}
