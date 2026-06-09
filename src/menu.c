#include "menu.h"
#include "utils.h"
#include "cheats.h"
#include "input.h"
#include "hooks.h"
#include "uw.h"
#include "config_loader.h"
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx9.h"
#include <stdio.h>
#include <ctype.h>

int g_menu_open = 0;
int g_sw = 1280, g_sh = 720;
IDirect3DDevice9 *g_dev = NULL;
int g_sel = 0, g_tab = 0;

static int g_ready = 0, g_frame = 0;

static void *g_editing_item_ptr = NULL;
static char g_edit_buf[32] = {0};
static int g_edit_len = 0;
static DWORD g_edit_cursor_t = 0;

static Item health_items[] = {
    {"Invincibility", 0, &g_cheats.invincible},
    {"Extra Hearts", 0, NULL},
    {"Regenerate", 0, NULL},
    {"Breathe Underwater", 0, &g_cheats.underwater_breath},
};
static Item stud_items[] = {
    {"Infinite Studs", 0, &g_cheats.infinite_studs},
    {"Force Custom Value", 0, &g_cheats.force_custom_studs},
    {"Custom Studs", 1, &g_cheats.custom_stud_value, 0, 999999999},
    {"Force Golden Bricks", 0, &g_cheats.force_golden_bricks},
    {"Golden Bricks", 1, &g_cheats.golden_brick_value, 0, 999},
    {"Stud Magnet", 0, NULL},
    {"Score Multiplier", 1, &g_cheats.score_mult, 1, 10},
};
static Item fun_items[] = {
    {"Super Speed", 0, &g_cheats.super_speed},
    {"Y Velocity", 0, &g_cheats.super_jump},
    {"  Y Strength", 1, &g_cheats.super_jump_scale, -10, 1000},
    {"Character Scale", 0, &g_cheats.char_scale},
    {"  Scale %", 1, &g_cheats.char_scale_val, 10, 1000},
    {"NoClip", 0, &g_cheats.noclip},
    {"Time Freeze", 0, &g_cheats.time_freeze},
    {"Speed Mult", 1, &g_cheats.speed_mult, 1, 100},
};
static Item visual_items[] = {
    {"FPS Counter", 0, &g_cheats.show_fps},
    {"Debug Info (F2)", 0, &g_cheats.show_debug},
    {"Remove Water", 0, &g_cheats.remove_water},
};

static Item uw_items[] = {
    {"Enable Ultra-Wide", 0, &g_cheats.uw_enabled},
    {"  Ratio: Auto", 0, NULL},
    {"  Ratio: 16:9", 0, NULL},
    {"  Ratio: 21:9", 0, NULL},
    {"  Ratio: 32:9", 0, NULL},
};

static int uw_items_cb(int sel) {
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

Tab tabs[] = {
    {"Health", health_items, 4, NULL},
    {"Studs", stud_items, 7, NULL},
    {"Fun", fun_items, 8, NULL},
    {"Visual", visual_items, 3, NULL},
    {"UltraWide", uw_items, 5, uw_items_cb},
};

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
    
    /* curated Dark Gold & Sea-Teal Color Palette */
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

static int key_pressed(int vk) {
    int down = g_key_states[vk];
    int press = down && !g_keys[vk];
    g_keys[vk] = down;
    return press;
}

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

void menu_update_input(void) {
    uw_sync_state();
    // Fallback: poll F1 directly via GetAsyncKeyState in case the game
    // intercepts it via DirectInput before WndProc sees it.
    {
        static int prev_f1 = 0;
        int cur_f1 = (GetAsyncKeyState(VK_F1) & 0x8000) ? 1 : 0;
        if (cur_f1 && !prev_f1) {
            LOG("menu_update_input: F1 detected via GetAsyncKeyState");
            g_key_states[VK_F1] = 1;
        }
        prev_f1 = cur_f1;
    }

    if (key_pressed(VK_F1)) {
        menu_toggle();
    }

    if (!g_menu_open) {
        if (key_pressed(VK_F2)) g_cheats.show_debug = !g_cheats.show_debug;
        return;
    }

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
    if (key_pressed(VK_LEFT))  { g_tab = (g_tab - 1 + TAB_COUNT) % TAB_COUNT; g_sel = 0; }
    if (key_pressed(VK_RIGHT)) { g_tab = (g_tab + 1) % TAB_COUNT; g_sel = 0; }
    if (key_pressed(VK_RETURN)) {
        Tab *cur_tab = &tabs[g_tab];
        if (cur_tab->on_select && cur_tab->on_select(g_sel)) return;
        Item *it = &cur_tab->items[g_sel];
        if (it->type == 3) return; /* separator */
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

static int g_fps = 0, g_fc = 0;
static DWORD g_fps_t = 0;

static void update_fps(void) {
    g_fc++;
    DWORD now = GetTickCount();
    if (now - g_fps_t >= 1000) { g_fps = g_fc; g_fc = 0; g_fps_t = now; }
}

void menu_render(void) {
    if (!g_menu_open) return;
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

    Tab *cur = &tabs[g_tab];
    int items_y = tab_y + 32;
    for (int i = 0; i < cur->count; i++) {
        int iy = items_y + i * (ITEM_H + 4);
        Item *it = &cur->items[i];

        if (it->type == 3) {
            dl->AddRectFilled(ImVec2((float)(mx+16), (float)(iy+ITEM_H/2)), ImVec2((float)(mx+MENU_W-16), (float)(iy+ITEM_H/2+1)), IM_COL32(80,80,80,255));
            continue;
        }

        if (i % 2 == 0)
            dl->AddRectFilled(ImVec2((float)(mx+8), (float)iy), ImVec2((float)(mx+MENU_W-8), (float)(iy+ITEM_H)), IM_COL32(255,255,255,16));

        if (i == g_sel) {
            dl->AddRectFilled(ImVec2((float)(mx+4), (float)iy), ImVec2((float)(mx+7), (float)(iy+ITEM_H)), IM_COL32(204, 163, 71, 255));
            dl->AddRectFilled(ImVec2((float)(mx+8), (float)iy), ImVec2((float)(mx+MENU_W-8), (float)(iy+ITEM_H)), IM_COL32(30, 148, 135, 48));
        }

        ImU32 text_c = (i == g_sel) ? IM_COL32(255,255,255,255) : IM_COL32(200,200,200,255);
        dl->AddText(ImVec2((float)(mx+18), (float)(iy + (ITEM_H - (int)ImGui::CalcTextSize(it->name).y) / 2)), text_c, it->name);

        if (it->type == 2) {
            int btn_x = mx + MENU_W - 80;
            dl->AddRectFilled(ImVec2((float)btn_x, (float)(iy+3)), ImVec2((float)(btn_x+64), (float)(iy+ITEM_H-3)), IM_COL32(30, 148, 135, 255));
            const char *btn = "> EXEC";
            ImVec2 bs = ImGui::CalcTextSize(btn);
            dl->AddText(ImVec2((float)(btn_x + (64 - (int)bs.x) / 2), (float)(iy + (ITEM_H - (int)bs.y) / 2)), IM_COL32(255,255,255,255), btn);
        } else if (it->type == 0 && it->val) {
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
        } else if (it->type == 0 && !it->val && g_tab == 4) {
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
        } else if (it->type == 1 && it->val) {
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
        }
    }

    int foot_y = my + MENU_H - 24;
    dl->AddRectFilled(ImVec2((float)(mx+8), (float)(foot_y-4)), ImVec2((float)(mx+MENU_W-8), (float)(foot_y-3)), IM_COL32(64,64,64,255));
    const char *foot = "ENTER: Toggle/Edit  |  ARROWS: Navigate  |  ESC: Cancel Edit";
    ImVec2 fs = ImGui::CalcTextSize(foot);
    dl->AddText(ImVec2((float)(mx + (MENU_W - (int)fs.x) / 2), (float)foot_y), IM_COL32(160,160,160,255), foot);
}

void menu_render_overlay(void) {
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    const char *s = MOD_NAME " " MOD_VER " | F1: Menu | F2: Debug";
    int sw = g_sw; if (sw < 1) sw = 1280;
    ImVec2 ss = ImGui::CalcTextSize(s);
    dl->AddText(ImVec2((float)((sw - (int)ss.x) / 2), 2.0f), IM_COL32(204, 163, 71, 255), s);

    if (g_cheats.show_fps) {
        char buf[32]; snprintf(buf, sizeof(buf), "FPS: %d", g_fps);
        ImVec2 fs = ImGui::CalcTextSize(buf);
        dl->AddText(ImVec2((float)(sw - (int)fs.x - 10.0f), 25.0f), IM_COL32(30, 148, 135, 255), buf);
    }
}

void menu_render_debug(void) {
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
