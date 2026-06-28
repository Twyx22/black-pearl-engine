#ifndef BPE_INFINITE_CANNONBALLS_H
#define BPE_INFINITE_CANNONBALLS_H

/* ================================================================
 * Infinite Cannonballs Module (T3.13)
 *
 * Prevents cannonball/ammo count from being decremented when
 * firing, giving unlimited ammunition for ship combat and
 * cannon puzzles.
 *
 * Primary: native_cheat_set("CHEAT_INFINITE_TORPEDOS", 1)
 * Fallback: AOB scan for cannonball decrement patterns near
 *           the "CannonBall2" or "CHEAT_INFINITE_TORPEDOS"
 *           string references.
 * ================================================================ */

#include <windows.h>

void infinite_cannonballs_apply(void);
void infinite_cannonballs_remove(void);
int  infinite_cannonballs_get_enabled(void);

#endif /* BPE_INFINITE_CANNONBALLS_H */
