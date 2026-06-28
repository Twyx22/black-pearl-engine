#include "hotkeys.h"
#include "menu.h"
#include "cheats.h"
#include "input.h"
#include "utils.h"
#include <string.h>
#include <stdio.h>

/* ================================================================
 * Hotkey Customisation System Implementation
 *
 * Each frame, hotkeys_check() iterates all bound keys and toggles
 * the associated cheat item when the key transitions from up→down.
 *
 * Uses g_key_states[] (populated by WndProc / keyboard hooks) for
 * press detection.
 * ================================================================ */

HotkeyBinding g_hotkeys[HOTKEYS_MAX];
int g_hotkey_count = 0;

/* Previous frame's key states for edge detection */
static int s_prev_states[256] = {0};

/* -----------------------------------------------------------------
 * VK code → human-readable name lookup
 * ----------------------------------------------------------------- */
static const struct {
    int vk;
    const char *name;
} s_vk_names[] = {
    {VK_F1,      "F1"},
    {VK_F2,      "F2"},
    {VK_F3,      "F3"},
    {VK_F4,      "F4"},
    {VK_F5,      "F5"},
    {VK_F6,      "F6"},
    {VK_F7,      "F7"},
    {VK_F8,      "F8"},
    {VK_F9,      "F9"},
    {VK_F10,     "F10"},
    {VK_F11,     "F11"},
    {VK_F12,     "F12"},
    {VK_INSERT,  "Ins"},
    {VK_DELETE,  "Del"},
    {VK_HOME,    "Home"},
    {VK_END,     "End"},
    {VK_PRIOR,   "PgUp"},
    {VK_NEXT,    "PgDn"},
    {VK_LEFT,    "Left"},
    {VK_RIGHT,   "Right"},
    {VK_UP,      "Up"},
    {VK_DOWN,    "Down"},
    {VK_SPACE,   "Space"},
    {VK_RETURN,  "Enter"},
    {VK_ESCAPE,  "Esc"},
    {VK_TAB,     "Tab"},
    {VK_SHIFT,   "Shift"},
    {VK_CONTROL, "Ctrl"},
    {VK_MENU,    "Alt"},
    {VK_BACK,    "Back"},
    {VK_OEM_1,   ";"},
    {VK_OEM_2,   "/"},
    {VK_OEM_3,   "`"},
    {VK_OEM_4,   "["},
    {VK_OEM_5,   "\\"},
    {VK_OEM_6,   "]"},
    {VK_OEM_7,   "'"},
    {VK_OEM_COMMA,  ","},
    {VK_OEM_PERIOD, "."},
    {VK_OEM_MINUS,  "-"},
    {VK_OEM_PLUS,   "="},
    {VK_NUMPAD0, "N0"},   {VK_NUMPAD1, "N1"},
    {VK_NUMPAD2, "N2"},   {VK_NUMPAD3, "N3"},
    {VK_NUMPAD4, "N4"},   {VK_NUMPAD5, "N5"},
    {VK_NUMPAD6, "N6"},   {VK_NUMPAD7, "N7"},
    {VK_NUMPAD8, "N8"},   {VK_NUMPAD9, "N9"},
    {0, NULL}
};

const char* hotkeys_vk_name(int vk) {
    /* Printable ASCII */
    if (vk >= 'A' && vk <= 'Z') {
        static char buf[2];
        buf[0] = (char)vk;
        buf[1] = '\0';
        return buf;
    }
    if (vk >= '0' && vk <= '9') {
        static char buf[2];
        buf[0] = (char)vk;
        buf[1] = '\0';
        return buf;
    }
    /* Named keys */
    for (int i = 0; s_vk_names[i].name; i++) {
        if (s_vk_names[i].vk == vk)
            return s_vk_names[i].name;
    }
    return "???";
}

/* -----------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------- */
void hotkeys_init(void) {
    g_hotkey_count = 0;
    memset(g_hotkeys, 0, sizeof(g_hotkeys));
    memset(s_prev_states, 0, sizeof(s_prev_states));
}

void hotkeys_check(void) {
    if (g_hotkey_count == 0) {
        /* Still need to update prev states to avoid stale detection */
        memcpy(s_prev_states, g_key_states, sizeof(s_prev_states));
        return;
    }

    /* Filter out VK_F1 (menu toggle) and VK_F2 (debug toggle) from
     * hotkey processing — they have dedicated handlers in menu_update_input. */
    s_prev_states[VK_F1] = g_key_states[VK_F1];
    s_prev_states[VK_F2] = g_key_states[VK_F2];

    for (int i = 0; i < HOTKEYS_MAX; i++) {
        if (!g_hotkeys[i].active) continue;
        int vk = g_hotkeys[i].vk;
        if (vk < 0 || vk >= 256) continue;

        /* Rising edge detection */
        if (g_key_states[vk] && !s_prev_states[vk]) {
            int tab = g_hotkeys[i].tab;
            int item = g_hotkeys[i].item;

            /* Bound check */
            if (tab >= 0 && tab < 12 && item >= 0) {
                Item *it = menu_get_item(tab, item);
                if (it && it->type == 0 && it->val) {
                    /* Toggle the cheat */
                    *(int*)it->val = !*(int*)it->val;
                    LOG("Hotkey: %s toggled %s (tab=%d, item=%d)",
                        hotkeys_vk_name(vk), it->name, tab, item);
                }
            }
        }
    }

    memcpy(s_prev_states, g_key_states, sizeof(s_prev_states));
}

int hotkeys_bind(int vk, int tab, int item) {
    /* Check if already bound to this (tab, item) */
    int existing = hotkeys_find(tab, item);
    if (existing >= 0) {
        g_hotkeys[existing].vk = vk;
        LOG("Hotkeys: rebound slot %d to VK_%d", existing, vk);
        return existing;
    }

    /* Find a free slot */
    for (int i = 0; i < HOTKEYS_MAX; i++) {
        if (!g_hotkeys[i].active) {
            g_hotkeys[i].vk = vk;
            g_hotkeys[i].tab = tab;
            g_hotkeys[i].item = item;
            g_hotkeys[i].active = 1;
            if (i >= g_hotkey_count) g_hotkey_count = i + 1;
            LOG("Hotkeys: bound VK_%d to (tab=%d, item=%d) [slot %d]", vk, tab, item, i);
            return i;
        }
    }
    LOG("Hotkeys: no free slots (max %d)", HOTKEYS_MAX);
    return -1;
}

void hotkeys_unbind(int index) {
    if (index < 0 || index >= HOTKEYS_MAX) return;
    if (!g_hotkeys[index].active) return;
    g_hotkeys[index].active = 0;
    g_hotkeys[index].vk = 0;
    LOG("Hotkeys: unbound slot %d", index);

    /* Recompute g_hotkey_count */
    while (g_hotkey_count > 0 && !g_hotkeys[g_hotkey_count - 1].active)
        g_hotkey_count--;
}

void hotkeys_unbind_by_item(int tab, int item) {
    for (int i = 0; i < HOTKEYS_MAX; i++) {
        if (g_hotkeys[i].active &&
            g_hotkeys[i].tab == tab &&
            g_hotkeys[i].item == item) {
            hotkeys_unbind(i);
            return;
        }
    }
}

int hotkeys_find(int tab, int item) {
    for (int i = 0; i < HOTKEYS_MAX; i++) {
        if (g_hotkeys[i].active &&
            g_hotkeys[i].tab == tab &&
            g_hotkeys[i].item == item)
            return i;
    }
    return -1;
}

void hotkeys_clear(void) {
    memset(g_hotkeys, 0, sizeof(g_hotkeys));
    g_hotkey_count = 0;
    memset(s_prev_states, 0, sizeof(s_prev_states));
    LOG("Hotkeys: all cleared");
}
