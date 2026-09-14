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
/* Per-frame re-force of Tier 3 values (the game rewrites them).
 * Called from update_cheats() only while infinite_ammo is active. */
void infinite_ammo_force_frame(void);

#endif /* BPE_INFINITE_AMMO_H */
