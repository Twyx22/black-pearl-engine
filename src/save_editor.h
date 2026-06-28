#ifndef BPE_SAVE_EDITOR_H
#define BPE_SAVE_EDITOR_H

/* ================================================================
 * Save File Editor Module
 *
 * In-game save file viewer / editor.
 * Browses the game's save directory, reads .sav headers, displays
 * key fields (studs, bricks, levels), and allows editing.
 * Creates .bak backups before modifying.
 * ================================================================ */

void save_editor_init(void);
void save_editor_render(void);   /* ImGui window — call inside Present */
void save_editor_browse(void);   /* Find save files */

#endif /* BPE_SAVE_EDITOR_H */
