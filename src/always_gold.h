#ifndef BPE_ALWAYS_GOLD_H
#define BPE_ALWAYS_GOLD_H

/* ================================================================
 * Always Gold Module (T3.10)
 *
 * Forces the game's "always score multiply" cheat to be active,
 * ensuring every stud pickup is worth 10x by default.
 *
 * Uses the native cheat system: native_always_score_multiply(1)
 * is called via the convenience wrapper.
 * ================================================================ */

void always_gold_apply(void);
void always_gold_remove(void);
int  always_gold_get_enabled(void);

#endif /* BPE_ALWAYS_GOLD_H */
