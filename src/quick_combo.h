#ifndef BPE_QUICK_COMBO_H
#define BPE_QUICK_COMBO_H

/* ================================================================
 * Quick Combo Module (T3.11)
 *
 * Forces the combo system to behave as if the player has a fully
 * maxed-out combo multiplier.  Scans for references to known combo
 * strings (easy_combos, always_start_combos_first_anim) and patches
 * the controlling flags.
 *
 * Uses PatchRecord for clean apply/restore.
 * ================================================================ */

#include <windows.h>

void quick_combo_apply(void);
void quick_combo_remove(void);
int  quick_combo_get_enabled(void);

#endif /* BPE_QUICK_COMBO_H */
