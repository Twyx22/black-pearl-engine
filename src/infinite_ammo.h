#ifndef BPE_INFINITE_AMMO_H
#define BPE_INFINITE_AMMO_H

/* ================================================================
 * Infinite Ammo Module
 *
 * Three-tier strategy:
 *   1. Native cheat activation (CHEAT_INFINITE_TORPEDOS via strcmpi hook)
 *   2. AOB scan for ammo decrement instructions → NOP using PatchRecord
 *   3. Force max_ammo / torpedo_capacity to high value each frame
 * ================================================================ */

void infinite_ammo_apply(void);
void infinite_ammo_remove(void);

#endif /* BPE_INFINITE_AMMO_H */
