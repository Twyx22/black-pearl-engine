#include "menu.h"
#include "utils.h"
#include "cheats.h"
#include "input.h"
#include "hooks.h"
#include "uw.h"
#include "config_loader.h"
#include "presets.h"
#include "favorites.h"
#include "hotkeys.h"
#include "imgui.h"
#include "memory_browser.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx9.h"
#include <stdio.h>
#include <ctype.h>
#include <string.h>

int g_menu_open = 0;
int g_sw = 1280, g_sh = 720;
IDirect3DDevice9 *g_dev = NULL;
int g_sel = 0, g_tab = 0;

static int g_ready = 0, g_frame = 0;

static void *g_editing_item_ptr = NULL;
static char g_edit_buf[32] = {0};
static int g_edit_len = 0;
static DWORD g_edit_cursor_t = 0;

/* -----------------------------------------------------------------
 * F3 key state for toggling favourites (poll-based, like F1/F2)
 * ----------------------------------------------------------------- */
int g_prev_f3 = 0;

/* -----------------------------------------------------------------
 * Presets UI state
 * ----------------------------------------------------------------- */
static char g_preset_name_buf[48] = {0};
static int g_preset_name_len = 0;
static int g_preset_editing = 0; /* 1=editing name, 0=normal */
static char g_preset_list[20][48];
static int g_preset_count = 0;
static int g_preset_scroll = 0;

/* -----------------------------------------------------------------
 * Hotkeys capture state
 * ----------------------------------------------------------------- */
static int g_capturing_hotkey = 0;   /* 1 = waiting for a keypress */
static int g_capture_tab = 0;
static int g_capture_item = 0;

/* -----------------------------------------------------------------
 * Favourites tab: items are dynamically built from favourites list
 * ----------------------------------------------------------------- */
static Item g_fav_items[FAVORITES_MAX];
static int g_fav_item_count = 0;

/* -----------------------------------------------------------------
 * Item definitions for all 12 tabs
 * ----------------------------------------------------------------- */

/* --- Tab 0: Health --- */
static Item health_items[] = {
    {"Invincibility", 0, &g_cheats.invincible, 0, 0, NULL},
    {"Breathe Underwater", 0, &g_cheats.underwater_breath, 0, 0, NULL},
};

/* --- Tab 1: Damage --- */
static Item damage_items[] = {
    {"Reverse Damage", 0, &g_cheats.reverse_damage, 0, 0, NULL},
    {"One Heart Mode", 0, &g_cheats.one_heart, 0, 0, NULL},
    {"One-Hit-Kill", 0, &g_cheats.one_hit_kill, 0, 0, NULL},
    {"Damage Response Only", 0, &g_cheats.damage_response_only, 0, 0, NULL},
    {"No Knockback", 0, &g_cheats.no_knockback, 0, 0, NULL},
    {"No Hit Reactions", 0, &g_cheats.no_hit_reaction, 0, 0, NULL},
};

/* --- Tab 2: Studs --- */
static Item stud_items[] = {
    {"Infinite Studs", 0, &g_cheats.infinite_studs, 0, 0, NULL},
    {"Force Custom Value", 0, &g_cheats.force_custom_studs, 0, 0, NULL},
    {"  Custom Studs", 1, &g_cheats.custom_stud_value, 0, 999999999, NULL},
    {"Force Golden Bricks", 0, &g_cheats.force_golden_bricks, 0, 0, NULL},
    {"  Golden Bricks", 1, &g_cheats.golden_brick_value, 0, 999, NULL},
    {"Stud Magnet", 0, &g_cheats.stud_magnet, 0, 0, NULL},
    {"Score Multiplier", 1, &g_cheats.score_mult, 1, 10, NULL},
    {"Always Gold", 0, &g_cheats.always_gold, 0, 0, NULL},
};

/* --- Tab 3: Fun --- */
static Item fun_items[] = {
    {"Super Speed", 0, &g_cheats.super_speed, 0, 0, NULL},
    {"Y Velocity", 0, &g_cheats.super_jump, 0, 0, NULL},
    {"  Y Strength", 1, &g_cheats.super_jump_scale, -10, 1000, NULL},
    {"Character Scale", 0, &g_cheats.char_scale, 0, 0, NULL},
    {"  Scale %", 1, &g_cheats.char_scale_val, 10, 1000, NULL},
    {"NoClip", 0, &g_cheats.noclip, 0, 0, NULL},
    {"Time Freeze", 0, &g_cheats.time_freeze, 0, 0, NULL},
    {"Speed Mult", 1, &g_cheats.speed_mult, 1, 100, NULL},
    {"Quick Combo", 0, &g_cheats.quick_combo, 0, 0, NULL},
    {"Super Punch", 0, &g_cheats.super_punch, 0, 0, NULL},
    {"Infinite Ammo", 0, &g_cheats.infinite_ammo, 0, 0, NULL},
};

/* --- Tab 4: Visual --- */
static Item visual_items[] = {
    {"FPS Counter", 0, &g_cheats.show_fps, 0, 0, NULL},
    {"Debug Info (F2)", 0, &g_cheats.show_debug, 0, 0, NULL},
    {"Remove Water", 0, &g_cheats.remove_water, 0, 0, NULL},
    {"Memory Browser", 2, NULL, 0, 0, mem_browser_toggle},
};

/* --- Tab 5: UltraWide --- */
static Item uw_items[] = {
    {"Enable Ultra-Wide", 0, &g_cheats.uw_enabled, 0, 0, NULL},
    {"  Ratio: Auto", 0, NULL},
    {"  Ratio: 16:9", 0, NULL},
    {"  Ratio: 21:9", 0, NULL},
    {"  Ratio: 32:9", 0, NULL},
};

/* --- Tab 6: Ammo --- */
static Item ammo_items[] = {
    {"Infinite Cannonballs", 0, &g_cheats.infinite_cannonballs, 0, 0, NULL},
    {"Mega Destruct", 0, &g_cheats.mega_destruct, 0, 0, NULL},
};

/* --- Tab 7: Camera --- */
static Item camera_items[] = {
    {"Free Camera", 0, &g_cheats.free_camera, 0, 0, NULL},
    {"  FOV", 1, &g_cheats.fov, 30, 120, NULL},
    {"---", 3, NULL},
    {"Teleport Slot 0", 2, NULL, 0, 0, NULL, 0, 0, 100},
    {"Teleport Slot 1", 2, NULL, 0, 0, NULL, 0, 0, 101},
    {"Teleport Slot 2", 2, NULL, 0, 0, NULL, 0, 0, 102},
    {"Teleport Slot 3", 2, NULL, 0, 0, NULL, 0, 0, 103},
    {"Teleport Slot 4", 2, NULL, 0, 0, NULL, 0, 0, 104},
};

/* --- Tab 8: Memory (placeholder — opens memory browser) --- */
static Item memory_items[] = {
    {"Open Memory Browser", 2, NULL, 0, 0, mem_browser_toggle},
    {"---", 3},
    {"(Quick-jump not yet implemented)", 3},
};

/* --- Tab 9: Tools --- */
static Item tools_items[] = {
    {"Tools (Coming Soon)", 3, NULL},
    {"Squirrel Console", 2, NULL},
    {"Save Editor", 2, NULL},
    {"Level Editor", 2, NULL},
};

/* --- Tab 10: Presets --- */
/* Items are dynamically rebuilt in menu_render; the count is static. */
#define PRESETS_UI_ITEMS 8
static Item presets_ui_items[PRESETS_UI_ITEMS];
static int g_presets_ui_initialized = 0;

/* --- Tab 11: Hotkeys --- */
/* Items are dynamically rebuilt in menu_render. */
#define HOTKEYS_UI_ITEMS (HOTKEYS_MAX + 4)
static Item hotkeys_ui_items[HOTKEYS_UI_ITEMS];
static int g_hotkeys_ui_initialized = 0;

/* -----------------------------------------------------------------
 * Tab array: 12 tabs total
 * ----------------------------------------------------------------- */

/* Forward declare callbacks */
static int uw_radio_cb(int sel);
static int camera_items_cb(int sel);
static int memory_items_cb(int sel);

Tab tabs[] = {
    {"Health",   health_items,   sizeof(health_items)/sizeof(health_items[0]),  NULL, NULL},
    {"Damage",   damage_items,   sizeof(damage_items)/sizeof(damage_items[0]),  NULL, NULL},
    {"Studs",    stud_items,     sizeof(stud_items)/sizeof(stud_items[0]),      NULL, NULL},
    {"Fun",      fun_items,      sizeof(fun_items)/sizeof(fun_items[0]),        NULL, NULL},
    {"Visual",   visual_items,   sizeof(visual_items)/sizeof(visual_items[0]),  NULL, NULL},
    {"UltraWide",uw_items,       sizeof(uw_items)/sizeof(uw_items[0]),          uw_radio_cb, NULL},
    {"Ammo",     ammo_items,     sizeof(ammo_items)/sizeof(ammo_items[0]),      NULL, NULL},
    {"Camera",   camera_items,   sizeof(camera_items)/sizeof(camera_items[0]),  camera_items_cb, NULL},
    {"Memory",   memory_items,   sizeof(memory_items)/sizeof(memory_items[0]),  NULL, NULL},
    {"Tools",    tools_items,    sizeof(tools_items)/sizeof(tools_items[0]),    NULL, NULL},
    {"Presets",  presets_ui_items, PRESETS_UI_ITEMS,                            NULL, NULL},
    {"Hotkeys",  hotkeys_ui_items, HOTKEYS_UI_ITEMS,                            NULL, NULL},
};

/* -----------------------------------------------------------------
 * menu_get_item — bounds-checked access
 * ----------------------------------------------------------------- */
Item* menu_get_item(int tab, int item_idx) {
    if (tab < 0 || tab >= TAB_COUNT) return NULL;
    if (item_idx < 0 || item_idx >= tabs[tab].count) return NULL;
    return &tabs[tab].items[item_idx];
}

/* -----------------------------------------------------------------
 * Favourites: rebuild the dynamic Favourites tab items
 * ----------------------------------------------------------------- */
static void rebuild_fav_items(void) {
    g_fav_item_count = 0;
    int fcount = favorites_count();
    for (int i = 0; i < fcount && i < FAVORITES_MAX; i++) {
        const FavoriteEntry *fe = favorites_get(i);
        if (!fe) continue;
        Item *src = menu_get_item(fe->tab, fe->item);
        if (!src) continue;

        /* Build a label: "[TabName] ItemName" */
        char label[64];
        snprintf(label, sizeof(label), "[%s] %s",
                  tabs[fe->tab].name, src->name);

        g_fav_items[g_fav_item_count].name = strdup(label);
        g_fav_items[g_fav_item_count].type = src->type;
        g_fav_items[g_fav_item_count].val = src->val;
        g_fav_items[g_fav_item_count].min = src->min;
        g_fav_items[g_fav_item_count].max = src->max;
        g_fav_items[g_fav_item_count].action = src->action;
        g_fav_items[g_fav_item_count].favorite = 1;
        g_fav_items[g_fav_item_count].hotkey_vk = src->hotkey_vk;
        g_fav_items[g_fav_item_count].tag = fe->tab * 1000 + fe->item; /* encode source */
        g_fav_item_count++;
    }
}

/* -----------------------------------------------------------------
 * Presets UI: build the preset list items dynamically
 * ----------------------------------------------------------------- */
static void rebuild_presets_ui(void) {
    g_preset_count = presets_list(g_preset_list, 20);

    int idx = 0;
    /* Row 0: Save new preset */
    presets_ui_items[idx].name = "Save Current as Preset...";
    presets_ui_items[idx].type = 2;
    presets_ui_items[idx].val = NULL;
    presets_ui_items[idx].action = NULL;
    presets_ui_items[idx].tag = 500; /* save action */
    idx++;

    /* Row 1: Preset name editing (or separator) */
    if (g_preset_editing) {
        presets_ui_items[idx].name = "  [Type name and press Enter]";
        presets_ui_items[idx].type = 12; /* special: preset name edit */
    } else {
        presets_ui_items[idx].name = "---";
        presets_ui_items[idx].type = 3;
    }
    presets_ui_items[idx].val = NULL;
    presets_ui_items[idx].action = NULL;
    presets_ui_items[idx].tag = 501;
    idx++;

    /* Load/Delete preset list */
    for (int i = 0; i < g_preset_count && idx < PRESETS_UI_ITEMS - 2; i++) {
        char label[64];
        snprintf(label, sizeof(label), "%s [Load]", g_preset_list[i]);
        presets_ui_items[idx].name = strdup(label);
        presets_ui_items[idx].type = 2;
        presets_ui_items[idx].val = NULL;
        presets_ui_items[idx].action = NULL;
        presets_ui_items[idx].tag = 600 + i; /* load action */
        idx++;
    }

    /* Separator + Delete section */
    if (idx < PRESETS_UI_ITEMS - 1) {
        presets_ui_items[idx].name = "---";
        presets_ui_items[idx].type = 3;
        idx++;
    }

    for (int i = 0; i < g_preset_count && idx < PRESETS_UI_ITEMS; i++) {
        char label[64];
        snprintf(label, sizeof(label), "  DEL: %s", g_preset_list[i]);
        presets_ui_items[idx].name = strdup(label);
        presets_ui_items[idx].type = 2;
        presets_ui_items[idx].val = NULL;
        presets_ui_items[idx].action = NULL;
        presets_ui_items[idx].tag = 700 + i; /* delete action */
        idx++;
    }

    /* Pad remaining with separators */
    while (idx < PRESETS_UI_ITEMS) {
        presets_ui_items[idx].name = "---";
        presets_ui_items[idx].type = 3;
        idx++;
    }
    tabs[10].count = PRESETS_UI_ITEMS;
}

/* -----------------------------------------------------------------
 * Hotkeys UI: build the keybinding list
 * ----------------------------------------------------------------- */
static void rebuild_hotkeys_ui(void) {
    int idx = 0;

    /* Header: Capturing state */
    if (g_capturing_hotkey) {
        hotkeys_ui_items[idx].name = ">> Press a key to bind... [Esc=abort]";
        hotkeys_ui_items[idx].type = 12;
        hotkeys_ui_items[idx].tag = 800;
        idx++;
    } else {
        hotkeys_ui_items[idx].name = "Select cheat, press F3 to bind";
        hotkeys_ui_items[idx].type = 3;
        hotkeys_ui_items[idx].tag = 0;
        idx++;
    }

    if (idx < HOTKEYS_UI_ITEMS) {
        hotkeys_ui_items[idx].name = "---";
        hotkeys_ui_items[idx].type = 3;
        idx++;
    }

    /* Clear all */
    if (idx < HOTKEYS_UI_ITEMS) {
        hotkeys_ui_items[idx].name = "Clear All Bindings";
        hotkeys_ui_items[idx].type = 2;
        hotkeys_ui_items[idx].val = NULL;
        hotkeys_ui_items[idx].action = NULL;
        hotkeys_ui_items[idx].tag = 900;
        idx++;
    }

    if (idx < HOTKEYS_UI_ITEMS) {
        hotkeys_ui_items[idx].name = "---";
        hotkeys_ui_items[idx].type = 3;
        idx++;
    }

    /* List all active bindings */
    for (int i = 0; i < HOTKEYS_MAX && idx < HOTKEYS_UI_ITEMS; i++) {
        if (!g_hotkeys[i].active) continue;

        Item *src = menu_get_item(g_hotkeys[i].tab, g_hotkeys[i].item);
        const char *item_name = src ? src->name : "???";
        const char *tab_name = (g_hotkeys[i].tab >= 0 && g_hotkeys[i].tab < TAB_COUNT)
                               ? tabs[g_hotkeys[i].tab].name : "???";

        char label[80];
        snprintf(label, sizeof(label), "%s: [%s] %s  [Unbind]",
                  hotkeys_vk_name(g_hotkeys[i].vk),
                  tab_name, item_name);

        hotkeys_ui_items[idx].name = strdup(label);
        hotkeys_ui_items[idx].type = 2;
        hotkeys_ui_items[idx].val = NULL;
        hotkeys_ui_items[idx].action = NULL;
        hotkeys_ui_items[idx].tag = 1000 + i; /* unbind action */
        idx++;
    }

    /* Pad remaining */
    while (idx < HOTKEYS_UI_ITEMS) {
        hotkeys_ui_items[idx].name = "---";
        hotkeys_ui_items[idx].type = 3;
        idx++;
    }
    tabs[11].count = HOTKEYS_UI_ITEMS;
}

/* -----------------------------------------------------------------
 * ImGui initialisation (same as before, updated palette)
 * ----------------------------------------------------------------- */
void menu_init_imgui(IDirect3DDevice9 *d, HWND hwnd) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    io.Fonts->AddFontDefault();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX9_Init(d);

    ImGuiStyle &style = ImGui::GetStyle();

    /* Premium Spacing & Padding */
    style.WindowPadding = ImVec2(15.0f, 15.0f);
    style.FramePadding = ImVec2(8.0f, 6.0f);
    style.ItemSpacing = ImVec2(10.0f, 8.0f);

    /* Modern Rounded Edges */
    style.WindowRounding = 8.0f;
    style.FrameRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 4.0f;

    /* Curated Dark Gold & Sea-Teal Color Palette */
    style.Colors[ImGuiCol_Text] = ImVec4(0.92f, 0.88f, 0.82f, 1.00f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.09f, 0.13f, 0.96f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.06f, 0.07f, 0.09f, 0.50f);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.09f, 0.13f, 0.98f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.80f, 0.64f, 0.28f, 0.80f);
    style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.14f, 0.18f, 0.54f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.12f, 0.58f, 0.53f, 0.40f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.12f, 0.58f, 0.53f, 0.67f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.80f, 0.64f, 0.28f, 0.25f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.80f, 0.64f, 0.28f, 0.40f);
    style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
    style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.80f, 0.64f, 0.28f, 0.40f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.80f, 0.64f, 0.28f, 0.60f);
    style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.80f, 0.64f, 0.28f, 0.80f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.18f, 0.75f, 0.68f, 1.00f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.12f, 0.58f, 0.53f, 1.00f);
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.18f, 0.75f, 0.68f, 1.00f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.80f, 0.64f, 0.28f, 0.25f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.12f, 0.58f, 0.53f, 0.80f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.18f, 0.75f, 0.68f, 1.00f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.12f, 0.58f, 0.53f, 0.31f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.12f, 0.58f, 0.53f, 0.80f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.18f, 0.75f, 0.68f, 1.00f);
    style.Colors[ImGuiCol_Separator] = ImVec4(0.80f, 0.64f, 0.28f, 0.50f);

    LOG("ImGui initialized");
}

void menu_release_imgui(void) {
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    LOG("ImGui shutdown");
}

void menu_toggle(void) {
    int was_open = g_menu_open;
    g_menu_open = !g_menu_open;
    g_editing_item_ptr = NULL;
    if (was_open && !g_menu_open) {
        save_config();
    }
}

/* -----------------------------------------------------------------
 * Key press detection (edge-triggered)
 * ----------------------------------------------------------------- */
static int key_pressed(int vk) {
    int down = g_key_states[vk];
    int press = down && !g_keys[vk];
    g_keys[vk] = down;
    return press;
}

/* -----------------------------------------------------------------
 * Tab callbacks
 * ----------------------------------------------------------------- */

/* UltraWide radio buttons */
static int uw_radio_cb(int sel) {
    if (sel == 0) {
        g_cheats.uw_enabled = !g_cheats.uw_enabled;
        g_uw_enabled = g_cheats.uw_enabled;
        if (g_uw_enabled) uw_apply_patches(); else uw_remove_patches();
        if (g_menu_open) save_config();
        return 1;
    }
    int ratios[] = {UW_RATIO_AUTO, UW_RATIO_16_9, UW_RATIO_21_9, UW_RATIO_32_9};
    int idx = sel - 1;
    if (idx >= 0 && idx < 4) {
        g_cheats.uw_ratio = ratios[idx];
        g_uw_ratio_mode = ratios[idx];
        uw_set_ratio(ratios[idx]);
        if (g_menu_open) save_config();
    }
    return 1;
}

/* Camera tab callback (teleport actions) */
static int camera_items_cb(int sel) {
    Item *it = &camera_items[sel];
    if (it->tag >= 100 && it->tag <= 104) {
        int slot = it->tag - 100;
        if (g_cheats.teleport_slot_0 || 1) { /* Always allow toggling */
            /* Toggle: save or load */
            static int slot_state[5] = {0};
            if (slot_state[slot] == 0) {
                /* Save current position to slot */
                extern void teleport_save(int);
                teleport_save(slot);
                slot_state[slot] = 1;
                it->name = "  [Saved]                  ";
            } else {
                /* Teleport to slot */
                extern void teleport_load(int);
                teleport_load(slot);
                slot_state[slot] = 0;
                it->name = "  Teleport Slot 0";
            }
        }
        return 1;
    }
    return 0;
}

/* Memory tab quick jumps */
/* -----------------------------------------------------------------
 * UW sync helper
 * ----------------------------------------------------------------- */
static void uw_sync_state(void) {
    static int prev_enabled = 0, prev_ratio = -1;
    if (g_cheats.uw_enabled != prev_enabled) {
        prev_enabled = g_cheats.uw_enabled;
        g_uw_enabled = g_cheats.uw_enabled;
        if (g_uw_enabled) uw_apply_patches(); else uw_remove_patches();
    }
    if (g_cheats.uw_ratio != prev_ratio) {
        prev_ratio = g_cheats.uw_ratio;
        g_uw_ratio_mode = g_cheats.uw_ratio;
        if (g_uw_enabled) uw_set_ratio(g_uw_ratio_mode);
    }
}

/* Forward declarations */
static void rebuild_current_tab_ui(void);
extern char g_preset_buf[48];

/* -----------------------------------------------------------------
 * Menu input update (called every frame from hk_EndScene)
 * ----------------------------------------------------------------- */
void menu_update_input(void) {
    uw_sync_state();

    /* Hotkey check (runs even when menu is closed) */
    hotkeys_check();

    /* Free camera input when active */
    if (g_cheats.free_camera) {
        free_camera_update();
    }

    /* Fallback: poll F1/F2/F3 directly via GetAsyncKeyState in case the game
     * intercepts them via DirectInput before WndProc sees them. */
    {
        static int prev_f1 = 0;
        int cur_f1 = (GetAsyncKeyState(VK_F1) & 0x8000) ? 1 : 0;
        if (cur_f1 && !prev_f1) {
            g_key_states[VK_F1] = 1;
        }
        prev_f1 = cur_f1;
    }
    {
        static int prev_f2 = 0;
        int cur_f2 = (GetAsyncKeyState(VK_F2) & 0x8000) ? 1 : 0;
        if (cur_f2 && !prev_f2) {
            g_key_states[VK_F2] = 1;
        }
        prev_f2 = cur_f2;
    }
    {
        int cur_f3 = (GetAsyncKeyState(VK_F3) & 0x8000) ? 1 : 0;
        if (cur_f3 && !g_prev_f3) {
            g_key_states[VK_F3] = 1;
        }
        g_prev_f3 = cur_f3;
    }

    /* F1: menu toggle */
    if (key_pressed(VK_F1)) {
        menu_toggle();
    }

    /* F2: debug overlay toggle (works even when menu is closed) */
    if (!g_menu_open) {
        if (key_pressed(VK_F2)) g_cheats.show_debug = !g_cheats.show_debug;
        return;
    }

    /* ---- Menu is open ---- */

    /* Hotkey capture mode: wait for any key */
    if (g_capturing_hotkey) {
        /* Check for escape to cancel */
        if (key_pressed(VK_ESCAPE)) {
            g_capturing_hotkey = 0;
            rebuild_hotkeys_ui();
            return;
        }
        /* Check for a valid VK code */
        for (int vk = 1; vk < 256; vk++) {
            if (vk == VK_F1 || vk == VK_F2 || vk == VK_F3) continue; /* Reserved */
            if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON) continue;
            if (!g_keys[vk] && g_key_states[vk]) {
                /* Valid key pressed — bind it */
                hotkeys_bind(vk, g_capture_tab, g_capture_item);
                g_capturing_hotkey = 0;
                rebuild_hotkeys_ui();
                /* Mark the item as having a hotkey */
                Item *it = menu_get_item(g_capture_tab, g_capture_item);
                if (it) it->hotkey_vk = vk;
                save_config();
                return;
            }
        }
        return;
    }

    /* Preset name editing mode */
    if (g_preset_editing) {
        if (key_pressed(VK_RETURN)) {
            if (g_preset_name_len > 0) {
                g_preset_buf[g_preset_name_len] = '\0';
                presets_save(g_preset_buf);
                g_preset_editing = 0;
                g_preset_name_len = 0;
                rebuild_presets_ui();
            }
            return;
        }
        if (key_pressed(VK_ESCAPE)) {
            g_preset_editing = 0;
            g_preset_name_len = 0;
            return;
        }
        if (key_pressed(VK_BACK)) {
            if (g_preset_name_len > 0) g_preset_name_len--;
            return;
        }
        /* Accept printable chars for preset name */
        for (int vk = 'A'; vk <= 'Z'; vk++) {
            if (key_pressed(vk) && g_preset_name_len < 46) {
                g_preset_buf[g_preset_name_len++] = (char)(vk + 32); /* lowercase */
                g_preset_buf[g_preset_name_len] = '\0';
                return;
            }
        }
        for (int vk = '0'; vk <= '9'; vk++) {
            if (key_pressed(vk) && g_preset_name_len < 46) {
                g_preset_buf[g_preset_name_len++] = (char)vk;
                g_preset_buf[g_preset_name_len] = '\0';
                return;
            }
        }
        return;
    }

    /* Value editing mode */
    if (g_editing_item_ptr) {
        if (key_pressed(VK_RETURN)) {
            if (g_edit_len > 0) {
                int val = atoi(g_edit_buf);
                Item *it = (Item*)g_editing_item_ptr;
                if (val < it->min) val = it->min;
                if (val > it->max) val = it->max;
                *(int*)it->val = val;
            }
            g_editing_item_ptr = NULL;
            return;
        }
        if (key_pressed(VK_ESCAPE)) {
            g_editing_item_ptr = NULL;
            return;
        }
        if (key_pressed(VK_BACK)) {
            if (g_edit_len > 0) g_edit_buf[--g_edit_len] = '\0';
            return;
        }
        for (int vk = '0'; vk <= '9'; vk++) {
            if (key_pressed(vk) && g_edit_len < 15) {
                g_edit_buf[g_edit_len++] = (char)vk;
                g_edit_buf[g_edit_len] = '\0';
                return;
            }
        }
        for (int vk = VK_NUMPAD0; vk <= VK_NUMPAD9; vk++) {
            if (key_pressed(vk) && g_edit_len < 15) {
                g_edit_buf[g_edit_len++] = '0' + (vk - VK_NUMPAD0);
                g_edit_buf[g_edit_len] = '\0';
                return;
            }
        }
        return;
    }

    /* Normal navigation */
    if (key_pressed(VK_UP)) {
        g_sel--;
        while (g_sel > 0 && tabs[g_tab].items[g_sel].type == 3) g_sel--;
        if (g_sel < 0) g_sel = 0;
    }
    if (key_pressed(VK_DOWN)) {
        g_sel++;
        while (g_sel < tabs[g_tab].count - 1 && tabs[g_tab].items[g_sel].type == 3) g_sel++;
        if (g_sel >= tabs[g_tab].count) g_sel = tabs[g_tab].count - 1;
    }
    if (key_pressed(VK_LEFT))  { g_tab = (g_tab - 1 + TAB_COUNT) % TAB_COUNT; g_sel = 0; rebuild_current_tab_ui(); }
    if (key_pressed(VK_RIGHT)) { g_tab = (g_tab + 1) % TAB_COUNT; g_sel = 0; rebuild_current_tab_ui(); }

    /* F3: Toggle favourite for current item */
    if (key_pressed(VK_F3)) {
        int result = favorites_toggle(g_tab, g_sel);
        Item *it = &tabs[g_tab].items[g_sel];
        it->favorite = result;
        if (g_menu_open) save_config();
    }

    /* Enter: activate selected item */
    if (key_pressed(VK_RETURN)) {
        Tab *cur_tab = &tabs[g_tab];
        if (cur_tab->on_select && cur_tab->on_select(g_sel)) return;
        Item *it = &cur_tab->items[g_sel];
        if (it->type == 3) return; /* separator */

        /* Handle tag-based actions */
        if (it->tag >= 500 && it->tag < 600) {
            /* Presets: save action */
            if (it->tag == 500) {
                g_preset_editing = 1;
                g_preset_name_len = 0;
                memset(g_preset_buf, 0, sizeof(g_preset_buf));
                rebuild_presets_ui();
                return;
            }
        }
        if (it->tag >= 600 && it->tag < 700) {
            /* Presets: load */
            int idx = it->tag - 600;
            if (idx >= 0 && idx < g_preset_count) {
                presets_load(g_preset_list[idx]);
                LOG("Preset loaded: %s", g_preset_list[idx]);
                return;
            }
        }
        if (it->tag >= 700 && it->tag < 800) {
            /* Presets: delete */
            int idx = it->tag - 700;
            if (idx >= 0 && idx < g_preset_count) {
                presets_delete(g_preset_list[idx]);
                rebuild_presets_ui();
                return;
            }
        }
        if (it->tag >= 900 && it->tag < 1000) {
            if (it->tag == 900) {
                hotkeys_clear();
                rebuild_hotkeys_ui();
                return;
            }
        }
        if (it->tag >= 1000 && it->tag < 1100) {
            /* Hotkeys: unbind */
            int idx = it->tag - 1000;
            if (idx >= 0 && idx < HOTKEYS_MAX) {
                hotkeys_unbind(idx);
                rebuild_hotkeys_ui();
                return;
            }
        }

        if (it->type == 2 && it->action) {
            it->action();
        } else if (it->type == 0 && it->val) {
            *(int*)it->val = !*(int*)it->val;
        } else if (it->type == 1 && it->val) {
            g_editing_item_ptr = it;
            g_edit_len = 0;
            g_edit_buf[0] = '\0';
            g_edit_cursor_t = GetTickCount();
        }
    }
}

/* Rebuild the current tab's dynamic UI if needed */
static void rebuild_current_tab_ui(void) {
    /* Only tabs 10 (Presets) and 11 (Hotkeys) and any fav tab are dynamic */
    if (g_tab == 10) rebuild_presets_ui();
    if (g_tab == 11) rebuild_hotkeys_ui();
}

/* -----------------------------------------------------------------
 * FPS counter
 * ----------------------------------------------------------------- */
static int g_fps = 0, g_fc = 0;
static DWORD g_fps_t = 0;

static void update_fps(void) {
    g_fc++;
    DWORD now = GetTickCount();
    if (now - g_fps_t >= 1000) { g_fps = g_fc; g_fc = 0; g_fps_t = now; }
}

/* -----------------------------------------------------------------
 * Menu render (custom ImDrawList rendering)
 * ----------------------------------------------------------------- */
void menu_render(void) {
    if (!g_menu_open) return;

    /* Rebuild dynamic tabs if needed */
    if (g_tab == 10) rebuild_presets_ui();
    if (g_tab == 11) rebuild_hotkeys_ui();

    ImDrawList *dl = ImGui::GetForegroundDrawList();
    int sw = g_sw, sh = g_sh;
    int mx = (sw - MENU_W) / 2, my = (sh - MENU_H) / 2;

    /* Shadow effect */
    for (int i = 1; i <= 4; i++)
        dl->AddRectFilled(ImVec2((float)(mx+i), (float)(my+i)), ImVec2((float)(mx+i+MENU_W), (float)(my+i+MENU_H)), IM_COL32(0,0,0,64));

    /* Main box (deep slate navy) */
    dl->AddRectFilled(ImVec2((float)mx, (float)my), ImVec2((float)(mx+MENU_W), (float)(my+MENU_H)), IM_COL32(20, 23, 33, 245));

    /* Gold borders */
    dl->AddRectFilled(ImVec2((float)mx, (float)my), ImVec2((float)(mx+MENU_W), (float)(my+1)), IM_COL32(204, 163, 71, 255));
    dl->AddRectFilled(ImVec2((float)mx, (float)(my+MENU_H-1)), ImVec2((float)(mx+MENU_W), (float)(my+MENU_H)), IM_COL32(204, 163, 71, 255));
    dl->AddRectFilled(ImVec2((float)mx, (float)my), ImVec2((float)(mx+1), (float)(my+MENU_H)), IM_COL32(204, 163, 71, 255));
    dl->AddRectFilled(ImVec2((float)(mx+MENU_W-1), (float)my), ImVec2((float)(mx+MENU_W), (float)(my+MENU_H)), IM_COL32(204, 163, 71, 255));

    /* Header banner (gold) */
    dl->AddRectFilled(ImVec2((float)mx, (float)my), ImVec2((float)(mx+MENU_W), (float)(my+36)), IM_COL32(204, 163, 71, 255));
    dl->AddRectFilled(ImVec2((float)mx, (float)(my+34)), ImVec2((float)(mx+MENU_W), (float)(my+36)), IM_COL32(170, 130, 45, 255));

    const char *title = MOD_NAME " " MOD_VER;
    ImVec2 ts = ImGui::CalcTextSize(title);
    dl->AddText(ImVec2((float)(mx + (MENU_W - (int)ts.x) / 2), (float)(my + (36 - (int)ts.y) / 2)), IM_COL32(0,0,0,255), title);

    /* Tab bar */
    float tw = (float)MENU_W / TAB_COUNT;
    int tab_y = my + 40;
    for (int i = 0; i < TAB_COUNT; i++) {
        int tx = mx + (int)(i * tw);
        if (i == g_tab) {
            dl->AddRectFilled(ImVec2((float)(tx+4), (float)(tab_y+2)), ImVec2((float)(tx+(int)tw-4), (float)(tab_y+24)), IM_COL32(30, 148, 135, 120));
            dl->AddRectFilled(ImVec2((float)(tx+4), (float)(tab_y+22)), ImVec2((float)(tx+(int)tw-4), (float)(tab_y+24)), IM_COL32(204, 163, 71, 255));
            const char *tn = tabs[i].name;
            ImVec2 tns = ImGui::CalcTextSize(tn);
            dl->AddText(ImVec2((float)(tx + ((int)tw - (int)tns.x) / 2), (float)(tab_y + (24 - (int)tns.y) / 2)), IM_COL32(204, 163, 71, 255), tn);
        } else {
            const char *tn = tabs[i].name;
            ImVec2 tns = ImGui::CalcTextSize(tn);
            dl->AddText(ImVec2((float)(tx + ((int)tw - (int)tns.x) / 2), (float)(tab_y + (24 - (int)tns.y) / 2)), IM_COL32(160, 160, 160, 255), tn);
        }
    }

    dl->AddRectFilled(ImVec2((float)(mx+8), (float)(tab_y+26)), ImVec2((float)(mx+MENU_W-8), (float)(tab_y+27)), IM_COL32(64,64,64,255));

    /* Item list */
    Tab *cur = &tabs[g_tab];
    int items_y = tab_y + 32;
    for (int i = 0; i < cur->count; i++) {
        int iy = items_y + i * (ITEM_H + 4);
        Item *it = &cur->items[i];

        if (it->type == 3) {
            /* Separator */
            dl->AddRectFilled(ImVec2((float)(mx+16), (float)(iy+ITEM_H/2)), ImVec2((float)(mx+MENU_W-16), (float)(iy+ITEM_H/2+1)), IM_COL32(80,80,80,255));
            continue;
        }

        /* Alternating row background */
        if (i % 2 == 0)
            dl->AddRectFilled(ImVec2((float)(mx+8), (float)iy), ImVec2((float)(mx+MENU_W-8), (float)(iy+ITEM_H)), IM_COL32(255,255,255,16));

        /* Selection highlight */
        if (i == g_sel) {
            dl->AddRectFilled(ImVec2((float)(mx+4), (float)iy), ImVec2((float)(mx+7), (float)(iy+ITEM_H)), IM_COL32(204, 163, 71, 255));
            dl->AddRectFilled(ImVec2((float)(mx+8), (float)iy), ImVec2((float)(mx+MENU_W-8), (float)(iy+ITEM_H)), IM_COL32(30, 148, 135, 48));
        }

        ImU32 text_c = (i == g_sel) ? IM_COL32(255,255,255,255) : IM_COL32(200,200,200,255);

        /* Favourite star indicator */
        if (it->favorite) {
            dl->AddText(ImVec2((float)(mx+10), (float)(iy + (ITEM_H - (int)ImGui::CalcTextSize(it->name).y) / 2)), IM_COL32(204, 163, 71, 255), "*");
        }
        dl->AddText(ImVec2((float)(mx+22), (float)(iy + (ITEM_H - (int)ImGui::CalcTextSize(it->name).y) / 2)), text_c, it->name);

        /* Hotkey indicator */
        if (it->hotkey_vk) {
            const char *vk_name = hotkeys_vk_name(it->hotkey_vk);
            int hk_x = mx + MENU_W - 140;
            ImVec2 hks = ImGui::CalcTextSize(vk_name);
            dl->AddText(ImVec2((float)(hk_x - (int)hks.x - 4), (float)(iy + (ITEM_H - (int)hks.y) / 2)), IM_COL32(160, 160, 80, 180), vk_name);
        }

        /* Right-side widget */
        if (it->type == 2) {
            /* Action button */
            int btn_x = mx + MENU_W - 80;
            dl->AddRectFilled(ImVec2((float)btn_x, (float)(iy+3)), ImVec2((float)(btn_x+64), (float)(iy+ITEM_H-3)), IM_COL32(30, 148, 135, 255));

            const char *btn = "> DO";
            if (it->tag >= 600 && it->tag < 700) btn = "Load";
            else if (it->tag >= 700 && it->tag < 800) btn = "DEL";
            else if (it->tag >= 1000 && it->tag < 1100) btn = "Unbd";
            else if (it->tag >= 500 && it->tag < 600) btn = "Save";

            ImVec2 bs = ImGui::CalcTextSize(btn);
            dl->AddText(ImVec2((float)(btn_x + (64 - (int)bs.x) / 2), (float)(iy + (ITEM_H - (int)bs.y) / 2)), IM_COL32(255,255,255,255), btn);
        } else if (it->type == 0 && it->val) {
            /* Toggle ON/OFF */
            int val = *(int*)it->val;
            int toggle_x = mx + MENU_W - 70;
            int toggle_y = iy + 4;
            int toggle_w = 50;
            int toggle_h = ITEM_H - 8;
            if (val) {
                dl->AddRectFilled(ImVec2((float)toggle_x, (float)toggle_y), ImVec2((float)(toggle_x+toggle_w), (float)(toggle_y+toggle_h)), IM_COL32(30, 148, 135, 255));
                dl->AddText(ImVec2((float)(toggle_x + (toggle_w - (int)ImGui::CalcTextSize("ON").x) / 2), (float)(toggle_y + (toggle_h - (int)ImGui::CalcTextSize("ON").y) / 2)), IM_COL32(255,255,255,255), "ON");
            } else {
                dl->AddRectFilled(ImVec2((float)toggle_x, (float)toggle_y), ImVec2((float)(toggle_x+toggle_w), (float)(toggle_y+toggle_h)), IM_COL32(68,68,68,255));
                dl->AddText(ImVec2((float)(toggle_x + (toggle_w - (int)ImGui::CalcTextSize("OFF").x) / 2), (float)(toggle_y + (toggle_h - (int)ImGui::CalcTextSize("OFF").y) / 2)), IM_COL32(136,136,136,255), "OFF");
            }
        } else if (it->type == 0 && !it->val) {
            /* Radio button style for UW ratio (tab 5) */
            if (g_tab == 5) {
                int rw = 16;
                int rx = mx + MENU_W - 30;
                int ry = iy + (ITEM_H - rw) / 2;
                int selected = 0;
                if (i == 1) selected = (g_uw_ratio_mode == UW_RATIO_AUTO);
                else if (i == 2) selected = (g_uw_ratio_mode == UW_RATIO_16_9);
                else if (i == 3) selected = (g_uw_ratio_mode == UW_RATIO_21_9);
                else if (i == 4) selected = (g_uw_ratio_mode == UW_RATIO_32_9);
                if (selected) {
                    dl->AddRectFilled(ImVec2((float)(rx+2), (float)(ry+2)), ImVec2((float)(rx+rw-2), (float)(ry+rw-2)), IM_COL32(204, 163, 71, 255));
                }
                dl->AddRect(ImVec2((float)rx, (float)ry), ImVec2((float)(rx+rw), (float)(ry+rw)), IM_COL32(200,200,200,255));
            }
        } else if (it->type == 1 && it->val) {
            /* Value edit widget */
            if (g_editing_item_ptr == it) {
                int edit_x = mx + MENU_W - 130;
                dl->AddRectFilled(ImVec2((float)edit_x, (float)(iy+2)), ImVec2((float)(edit_x+120), (float)(iy+ITEM_H-2)), IM_COL32(10,10,10,255));
                dl->AddRectFilled(ImVec2((float)edit_x, (float)(iy+2)), ImVec2((float)(edit_x+120), (float)(iy+3)), IM_COL32(204, 163, 71, 255));
                dl->AddRectFilled(ImVec2((float)edit_x, (float)(iy+ITEM_H-3)), ImVec2((float)(edit_x+120), (float)(iy+ITEM_H-2)), IM_COL32(204, 163, 71, 255));
                char buf[48];
                int show_cursor = ((GetTickCount() - g_edit_cursor_t) / 500) % 2;
                snprintf(buf, sizeof(buf), "%s%s", g_edit_buf, show_cursor ? "_" : "");
                ImVec2 es = ImGui::CalcTextSize(buf);
                dl->AddText(ImVec2((float)(edit_x + 116 - (int)es.x), (float)(iy + (ITEM_H - (int)es.y) / 2)), IM_COL32(204, 163, 71, 255), buf);
            } else {
                int val = *(int*)it->val;
                char buf[32]; snprintf(buf, sizeof(buf), "%d", val);
                int val_w = 80;
                int val_x = mx + MENU_W - val_w - 8;
                dl->AddRectFilled(ImVec2((float)val_x, (float)(iy+4)), ImVec2((float)(val_x+val_w), (float)(iy+ITEM_H-4)), IM_COL32(204, 163, 71, 48));
                ImVec2 vs = ImGui::CalcTextSize(buf);
                dl->AddText(ImVec2((float)(val_x + (val_w - (int)vs.x) / 2), (float)(iy + (ITEM_H - (int)vs.y) / 2)), IM_COL32(204, 163, 71, 255), buf);
            }
        } else if (it->type == 12) {
            /* Special: preset name edit */
            if (g_preset_editing) {
                char display[64];
                int show_cursor = ((GetTickCount() - g_edit_cursor_t) / 500) % 2;
                snprintf(display, sizeof(display), "Name: %s%s", g_preset_buf, show_cursor ? "_" : "");
                ImVec2 ds = ImGui::CalcTextSize(display);
                dl->AddText(ImVec2((float)(mx + 22), (float)(iy + (ITEM_H - (int)ds.y) / 2)), IM_COL32(204, 163, 71, 255), display);
            }
        }
    }

    /* Footer */
    int foot_y = my + MENU_H - 24;
    dl->AddRectFilled(ImVec2((float)(mx+8), (float)(foot_y-4)), ImVec2((float)(mx+MENU_W-8), (float)(foot_y-3)), IM_COL32(64,64,64,255));
    const char *foot = "ENTER: Toggle/Edit  |  ARROWS: Navigate  |  F3: Fav  |  ESC: Cancel";
    ImVec2 fs = ImGui::CalcTextSize(foot);
    dl->AddText(ImVec2((float)(mx + (MENU_W - (int)fs.x) / 2), (float)foot_y), IM_COL32(160,160,160,255), foot);
}

void menu_render_overlay(void) {
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    const char *s = MOD_NAME " " MOD_VER " | F1: Menu | F2: Debug | F3: Fav";
    int sw = g_sw; if (sw < 1) sw = 1280;
    ImVec2 ss = ImGui::CalcTextSize(s);
    dl->AddText(ImVec2((float)((sw - (int)ss.x) / 2), 2.0f), IM_COL32(204, 163, 71, 255), s);

    if (g_cheats.show_fps) {
        char buf[32]; snprintf(buf, sizeof(buf), "FPS: %d", g_fps);
        ImVec2 fs = ImGui::CalcTextSize(buf);
        dl->AddText(ImVec2((float)(sw - (int)fs.x - 10.0f), 25.0f), IM_COL32(30, 148, 135, 255), buf);
    }

    /* Free camera indicator */
    if (g_cheats.free_camera) {
        const char *cam = "FREE CAMERA ACTIVE";
        ImVec2 cs = ImGui::CalcTextSize(cam);
        dl->AddText(ImVec2((float)((sw - (int)cs.x) / 2), 22.0f), IM_COL32(255, 100, 100, 255), cam);
    }
}

void menu_render_debug(void) {
    mem_browser_render();
    if (!g_cheats.show_debug) return;

    /* Render interactive ImGui Window for Game Strings Debugger */
    ImGui::SetNextWindowSize(ImVec2(380, 420), ImGuiCond_FirstUseEver);
    bool open = true;
    if (ImGui::Begin("Game Strings Debugger", &open, ImGuiWindowFlags_NoCollapse)) {
        static char search_query[64] = {0};
        ImGui::InputText("Search Filter", search_query, sizeof(search_query));
        ImGui::Separator();

        ImGui::BeginChild("ScrollingRegion", ImVec2(0, 0), true);
        if (g_entity_count <= 0) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "No entities found in memory.");
        } else {
            int displayed = 0;
            for (int i = 0; i < g_entity_count; i++) {
                const char *str = g_entities[i];
                if (!str) continue;

                /* Case-insensitive query filter */
                if (search_query[0] != '\0') {
                    int match = 0;
                    const char *s1 = str;
                    const char *s2 = search_query;
                    while (*s1) {
                        const char *h = s1;
                        const char *n = s2;
                        while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
                            h++;
                            n++;
                        }
                        if (!*n) {
                            match = 1;
                            break;
                        }
                        s1++;
                    }
                    if (!match) continue;
                }

                ImGui::Text("%d: %s", i, str);
                displayed++;
            }
            if (displayed == 0) {
                ImGui::TextColored(ImVec4(0.8f, 0.64f, 0.28f, 1.0f), "No matching strings.");
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
    if (!open) {
        g_cheats.show_debug = 0;
    }
}

int menu_should_be_ready(void) {
    return g_ready;
}

int menu_get_frame(void) {
    return g_frame;
}

void menu_increment_frame(void) {
    g_frame++;
    if (!g_ready && g_frame > 90) {
        g_ready = 1;
        LOG("Ready at frame %d", g_frame);
    }
    update_fps();
}

/* Hidden global for preset name editing buffer */
char g_preset_buf[48];
