#ifndef BPE_LEVEL_EDITOR_H
#define BPE_LEVEL_EDITOR_H

/* ================================================================
 * Level Editor Integration Module
 *
 * The game has a built-in LevelEditor class (constructor at
 * base+0x18EB50, originally 0x58EB50).  This module provides
 * a way to launch / call it from the menu and check if it is
 * currently active.
 *
 * The LevelEditor is a singleton managed by
 * ClassManagerAccessor<LevelEditor>.
 * RTTI: .?AVLevelEditor@@
 * ================================================================ */

void level_editor_launch(void);
int  level_editor_is_active(void);

#endif /* BPE_LEVEL_EDITOR_H */
