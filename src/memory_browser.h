#ifndef BPE_MEMORY_BROWSER_H
#define BPE_MEMORY_BROWSER_H

#include <windows.h>

/* ================================================================
 * Memory Browser Module
 *
 * In-game hex memory viewer/editor with:
 *   - Address input (hex)
 *   - 16-byte hex dump with ASCII sidebar
 *   - Color-coded bytes (valid/code/invalid)
 *   - Byte editing (click + type hex)
 *   - AOB pattern search
 *   - Freeze values (force-write every frame)
 *   - Quick-jump to .text/.data/.rdata sections
 * ================================================================ */

void mem_browser_init(void);
void mem_browser_render(void);   /* Call inside ImGui frame (after NewFrame) */
void mem_browser_shutdown(void);
void mem_browser_toggle(void);   /* Open/close the browser window */

int  mem_browser_is_open(void);

#endif /* BPE_MEMORY_BROWSER_H */
