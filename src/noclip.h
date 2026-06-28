#ifndef BPE_NOCLIP_H
#define BPE_NOCLIP_H

/* ================================================================
 * NoClip Module for Black Pearl Engine
 *
 * Disables player collision detection allowing the character to
 * walk through walls and geometry.
 *
 * Dual approach:
 *   1. Primary: Scan .text for CALL instructions targeting the
 *      collision handler (sub_6AC280, base+0x2AC280) and NOP them.
 *   2. Secondary: Find references to collision-disable property
 *      strings and force the branch to always take the skip path.
 * ================================================================ */

void noclip_apply(void);
void noclip_remove(void);

#endif /* BPE_NOCLIP_H */
