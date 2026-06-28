#ifndef BPE_HOTKEYS_H
#define BPE_HOTKEYS_H

#include <windows.h>

/* ================================================================
 * Hotkey Customisation System (T4.4)
 *
 * Bind any VK key to toggle any cheat item.
 * 32 hotkey bindings max.
 * ================================================================ */

#define HOTKEYS_MAX 32

typedef struct {
    int vk;       /* Virtual key code (0 = unbound) */
    int tab;      /* Tab index */
    int item;     /* Item index within that tab */
    int active;   /* 1 if this slot is in use */
} HotkeyBinding;

extern HotkeyBinding g_hotkeys[HOTKEYS_MAX];
extern int g_hotkey_count;

/** Initialise hotkeys (zero all bindings). */
void hotkeys_init(void);

/** Check all hotkeys and toggle matched items. Call every frame. */
void hotkeys_check(void);

/** Add or replace a hotkey binding. Returns binding index, or -1 on failure. */
int hotkeys_bind(int vk, int tab, int item);

/** Remove a hotkey by binding index. */
void hotkeys_unbind(int index);

/** Remove a hotkey by (tab, item) pair. */
void hotkeys_unbind_by_item(int tab, int item);

/** Find the hotkey binding index for a (tab, item), or -1. */
int hotkeys_find(int tab, int item);

/** Clear all hotkey bindings. */
void hotkeys_clear(void);

/** Get a human-readable name for a VK code. Returns static string. */
const char* hotkeys_vk_name(int vk);

#endif /* BPE_HOTKEYS_H */
