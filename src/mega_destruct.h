#ifndef BPE_MEGA_DESTRUCT_H
#define BPE_MEGA_DESTRUCT_H

/* ================================================================
 * Mega Destruct Module (T3.12)
 *
 * Makes all destructible objects/enemies instantly destroyable
 * with a single hit, greatly accelerating combat and puzzle
 * sections that require breaking objects.
 *
 * Primary: native_cheat_set("CHEAT_SELFDESTRUCT", 1)
 * Fallback: AOB scan for destructibility check patterns near
 *           the "CHEAT_SELFDESTRUCT" string reference.
 * ================================================================ */

#include <windows.h>

void mega_destruct_apply(void);
void mega_destruct_remove(void);
int  mega_destruct_get_enabled(void);

#endif /* BPE_MEGA_DESTRUCT_H */
