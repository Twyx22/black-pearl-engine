#ifndef BPE_SCORE_MULT_H
#define BPE_SCORE_MULT_H

/** Score Multiplier Module
 *
 * Wraps native_score_multiplier() to provide lifecycle management:
 *   score_mult_apply()   — activate the currently stored multiplier value
 *   score_mult_remove()  — deactivate all score multipliers
 *   score_mult_set(val)  — set the multiplier value (0=off, 2,4,6,8,10)
 *   score_mult_get()     — read the current multiplier value
 *
 * The module tracks the previously-applied multiplier to handle
 * transitions between values (deactivates old, activates new).
 * Integrates with update_cheats() via g_cheats.score_mult.
 */

/** Set score multiplier value: 0=off, 2,4,6,8,10.
 *  Value is stored in g_cheats.score_mult. Call score_mult_apply()
 *  to make it take effect. */
void score_mult_set(int multiplier);

/** Apply the currently stored multiplier (from g_cheats.score_mult).
 *  Handles transitions: if switching from one multiplier to another,
 *  deactivates the old one first. */
void score_mult_apply(void);

/** Deactivate all score multipliers. */
void score_mult_remove(void);

/** Return the current multiplier value (g_cheats.score_mult). */
int  score_mult_get(void);

#endif /* BPE_SCORE_MULT_H */
