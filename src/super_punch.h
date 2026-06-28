#ifndef BPE_SUPER_PUNCH_H
#define BPE_SUPER_PUNCH_H

/* ================================================================
 * Super Punch Module (T3.9)
 *
 * Makes every punch stun enemies, effectively acting as a
 * one-hit-stun for all melee attacks.
 *
 * Primary: native_cheat_set("CHEAT_SUPERSLAP", 1)
 * Fallback: AOB scan for punch/stun flag bytes near the
 *           "punch_always_stun" string reference.
 * ================================================================ */

#include <windows.h>

void super_punch_apply(void);
void super_punch_remove(void);
int  super_punch_get_enabled(void);

#endif /* BPE_SUPER_PUNCH_H */
