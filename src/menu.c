#include "menu.h"
#include "utils.h"
#include "cheats.h"
#include "input.h"
#include <stdio.h>

int g_menu_open = 0;
int g_sw = 1280, g_sh = 720;
IDirect3DDevice9 *g_dev = NULL;
int g_sel = 0, g_tab = 0;

static ID3DXFont *g_font = NULL;
static ID3DXFont *g_font_small = NULL;
static int g_ready = 0, g_frame = 0;

static void *g_editing_item_ptr = NULL;
static char g_edit_buf[32] = {0};
static int g_edit_len = 0;
static DWORD g_edit_cursor_t = 0;

static Item health_items[] = {
    {"Invincibility", 0, &g_cheats.invincible},
    {"Extra Hearts", 0, NULL},
    {"Regenerate", 0, NULL},
    {"Breathe Underwater", 0, NULL},
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
    {"Super Jump", 0, &g_cheats.super_jump},
    {"Moon Jump", 0, &g_cheats.moon_jump},
    {"NoClip", 0, &g_cheats.noclip},
    {"Time Freeze", 0, &g_cheats.time_freeze},
};
static Item visual_items[] = {
    {"FPS Counter", 0, &g_cheats.show_fps},
    {"Debug Info (F2)", 0, &g_cheats.show_debug},
};

Tab tabs[] = {
    {"Health", health_items, 4},
    {"Studs", stud_items, 7},
    {"Fun", fun_items, 5},
    {"Visual", visual_items, 2},
};

void menu_init_fonts(IDirect3DDevice9 *d) {
    if (g_font) return;
    D3DXCreateFontA(d, 16, 0, FW_BOLD, 0, FALSE, DEFAULT_CHARSET,
                   OUT_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                   "Arial", &g_font);
    D3DXCreateFontA(d, 13, 0, FW_NORMAL, 0, FALSE, DEFAULT_CHARSET,
                   OUT_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                   "Arial", &g_font_small);
    if (g_font) LOG("Fonts OK");
}

void menu_release_fonts(void) {
    if (g_font) { g_font->Release(); g_font = NULL; }
    if (g_font_small) { g_font_small->Release(); g_font_small = NULL; }
}

static int key_pressed(int vk) {
    int down = g_key_states[vk];
    int press = down && !g_keys[vk];
    g_keys[vk] = down;
    return press;
}

void menu_update_input(void) {
    if (key_pressed(VK_F1)) {
        g_menu_open = !g_menu_open;
        g_editing_item_ptr = NULL;
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
            if (g_edit_len > 0) {
                g_edit_buf[--g_edit_len] = '\0';
            }
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

    if (key_pressed(VK_UP))   { g_sel--; if (g_sel < 0) g_sel = 0; }
    if (key_pressed(VK_DOWN)) { g_sel++; if (g_sel >= tabs[g_tab].count) g_sel = tabs[g_tab].count - 1; }
    if (key_pressed(VK_LEFT))  { g_tab = (g_tab - 1 + TAB_COUNT) % TAB_COUNT; g_sel = 0; }
    if (key_pressed(VK_RIGHT)) { g_tab = (g_tab + 1) % TAB_COUNT; g_sel = 0; }
    if (key_pressed(VK_RETURN)) {
        Item *it = &tabs[g_tab].items[g_sel];
        if (it->type == 0 && it->val) {
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

typedef struct { float x, y, z, rhw; D3DCOLOR color; } Vertex;

static void draw_rect(IDirect3DDevice9 *d, float x, float y, float w, float h, D3DCOLOR c) {
    DWORD old_fvf;
    IDirect3DVertexShader9 *old_vs = NULL;
    IDirect3DPixelShader9 *old_ps = NULL;
    DWORD old_ablend, old_srcblend, old_destblend, old_zenable, old_zwrite;

    d->GetFVF(&old_fvf);
    d->GetVertexShader(&old_vs);
    d->GetPixelShader(&old_ps);
    d->GetRenderState(D3DRS_ALPHABLENDENABLE, &old_ablend);
    d->GetRenderState(D3DRS_SRCBLEND, &old_srcblend);
    d->GetRenderState(D3DRS_DESTBLEND, &old_destblend);
    d->GetRenderState(D3DRS_ZENABLE, &old_zenable);
    d->GetRenderState(D3DRS_ZWRITEENABLE, &old_zwrite);

    d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    d->SetRenderState(D3DRS_ZENABLE, FALSE);
    d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    d->SetVertexShader(NULL);
    d->SetPixelShader(NULL);

    Vertex v[4] = {
        {x,   y,   0, 1, c},
        {x+w, y,   0, 1, c},
        {x,   y+h, 0, 1, c},
        {x+w, y+h, 0, 1, c}
    };
    d->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    d->SetTexture(0, NULL);
    d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(Vertex));

    d->SetRenderState(D3DRS_ALPHABLENDENABLE, old_ablend);
    d->SetRenderState(D3DRS_SRCBLEND, old_srcblend);
    d->SetRenderState(D3DRS_DESTBLEND, old_destblend);
    d->SetRenderState(D3DRS_ZENABLE, old_zenable);
    d->SetRenderState(D3DRS_ZWRITEENABLE, old_zwrite);
    d->SetVertexShader(old_vs);
    d->SetPixelShader(old_ps);
    d->SetFVF(old_fvf);

    if (old_vs) old_vs->Release();
    if (old_ps) old_ps->Release();
}

void menu_render(IDirect3DDevice9 *d) {
    if (!g_menu_open || !g_font || !g_font_small) return;
    int sw = g_sw, sh = g_sh;
    int mx = (sw - MENU_W) / 2, my = (sh - MENU_H) / 2;

    for (int i = 1; i <= 4; i++) {
        draw_rect(d, (float)(mx + i), (float)(my + i), (float)MENU_W, (float)MENU_H, 0x40000000);
    }

    draw_rect(d, (float)mx, (float)my, (float)MENU_W, (float)MENU_H, 0xF0181818);
    draw_rect(d, (float)mx, (float)my, (float)MENU_W, 1, 0xFFFFC800);
    draw_rect(d, (float)mx, (float)my + MENU_H - 1, (float)MENU_W, 1, 0xFFFFC800);
    draw_rect(d, (float)mx, (float)my, 1, (float)MENU_H, 0xFFFFC800);
    draw_rect(d, (float)mx + MENU_W - 1, (float)my, 1, (float)MENU_H, 0xFFFFC800);

    draw_rect(d, (float)mx, (float)my, (float)MENU_W, 36, 0xFFFFC800);
    draw_rect(d, (float)mx, (float)my + 34, (float)MENU_W, 2, 0xFFD4A000);

    RECT r = {mx, my, mx+MENU_W, my+36};
    g_font->DrawTextA(NULL, MOD_NAME " " MOD_VER, -1, &r, DT_CENTER | DT_VCENTER, 0xFF000000);

    float tw = (float)MENU_W / TAB_COUNT;
    int tab_y = my + 40;
    for (int i = 0; i < TAB_COUNT; i++) {
        int tx = mx + (int)(i * tw);
        RECT tr = {tx, tab_y, tx+(int)tw, tab_y+24};
        if (i == g_tab) {
            draw_rect(d, (float)(tx + 4), (float)(tab_y + 2), (float)(tw - 8), 22, 0x60FFC800);
            draw_rect(d, (float)(tx + 4), (float)(tab_y + 22), (float)(tw - 8), 2, 0xFFFFC800);
            g_font_small->DrawTextA(NULL, tabs[i].name, -1, &tr, DT_CENTER | DT_VCENTER, 0xFFFFC800);
        } else {
            g_font_small->DrawTextA(NULL, tabs[i].name, -1, &tr, DT_CENTER | DT_VCENTER, 0xFF909090);
        }
    }

    draw_rect(d, (float)(mx + 8), (float)(tab_y + 26), (float)(MENU_W - 16), 1, 0xFF404040);

    Tab *cur = &tabs[g_tab];
    int items_y = tab_y + 32;
    for (int i = 0; i < cur->count; i++) {
        int iy = items_y + i * (ITEM_H + 4);
        Item *it = &cur->items[i];

        if (i % 2 == 0) {
            draw_rect(d, (float)(mx + 8), (float)iy, (float)(MENU_W - 16), (float)ITEM_H, 0x20FFFFFF);
        }

        if (i == g_sel) {
            draw_rect(d, (float)(mx + 4), (float)iy, 3, (float)ITEM_H, 0xFFFFC800);
            draw_rect(d, (float)(mx + 8), (float)iy, (float)(MENU_W - 16), (float)ITEM_H, 0x30FFC800);
        }

        RECT ir = {mx+18, iy, mx+MENU_W-80, iy+ITEM_H};
        D3DCOLOR text_c = (i == g_sel) ? 0xFFFFFFFF : 0xFFC8C8C8;
        g_font_small->DrawTextA(NULL, it->name, -1, &ir, DT_LEFT | DT_VCENTER, text_c);

        if (it->type == 0 && it->val) {
            int val = *(int*)it->val;
            int toggle_x = mx + MENU_W - 70;
            int toggle_y = iy + 4;
            int toggle_w = 50;
            int toggle_h = ITEM_H - 8;

            if (val) {
                draw_rect(d, (float)toggle_x, (float)toggle_y, (float)toggle_w, (float)toggle_h, 0xFF00AA00);
                RECT tr = {toggle_x, toggle_y, toggle_x+toggle_w, toggle_y+toggle_h};
                g_font_small->DrawTextA(NULL, "ON", -1, &tr, DT_CENTER | DT_VCENTER, 0xFFFFFFFF);
            } else {
                draw_rect(d, (float)toggle_x, (float)toggle_y, (float)toggle_w, (float)toggle_h, 0xFF444444);
                RECT tr = {toggle_x, toggle_y, toggle_x+toggle_w, toggle_y+toggle_h};
                g_font_small->DrawTextA(NULL, "OFF", -1, &tr, DT_CENTER | DT_VCENTER, 0xFF888888);
            }
        } else if (it->type == 1 && it->val) {
            if (g_editing_item_ptr == it) {
                int edit_x = mx + MENU_W - 130;
                draw_rect(d, (float)edit_x, (float)(iy + 2), 120, (float)(ITEM_H - 4), 0xFF0A0A0A);
                draw_rect(d, (float)edit_x, (float)(iy + 2), 120, 1, 0xFFFFC800);
                draw_rect(d, (float)edit_x, (float)(iy + ITEM_H - 5), 120, 1, 0xFFFFC800);
                char buf[48];
                int show_cursor = ((GetTickCount() - g_edit_cursor_t) / 500) % 2;
                snprintf(buf, sizeof(buf), "%s%s", g_edit_buf, show_cursor ? "_" : "");
                RECT vr = {edit_x + 4, iy, mx+MENU_W-8, iy+ITEM_H};
                g_font_small->DrawTextA(NULL, buf, -1, &vr, DT_RIGHT | DT_VCENTER, 0xFFFFC800);
            } else {
                int val = *(int*)it->val;
                char buf[32]; snprintf(buf, sizeof(buf), "%d", val);
                int val_w = 80;
                int val_x = mx + MENU_W - val_w - 8;
                draw_rect(d, (float)val_x, (float)(iy + 4), (float)val_w, (float)(ITEM_H - 8), 0x30FFFFFF);
                RECT vr = {val_x, iy, val_x+val_w, iy+ITEM_H};
                g_font_small->DrawTextA(NULL, buf, -1, &vr, DT_CENTER | DT_VCENTER, 0xFFFFC800);
            }
        }
    }

    int foot_y = my + MENU_H - 24;
    draw_rect(d, (float)(mx + 8), (float)(foot_y - 4), (float)(MENU_W - 16), 1, 0xFF404040);
    RECT fr = {mx, foot_y, mx+MENU_W, foot_y+20};
    g_font_small->DrawTextA(NULL, "ENTER: Toggle/Edit  |  ARROWS: Navigate  |  ESC: Cancel Edit", -1, &fr, DT_CENTER | DT_VCENTER, 0xFF808080);
}

void menu_render_overlay(IDirect3DDevice9 *d) {
    if (!g_font_small) return;
    int sw = g_sw; if (sw < 1) sw = 1280;

    RECT r = {0, 0, sw, 22};
    g_font_small->DrawTextA(NULL, MOD_NAME " " MOD_VER " | F1: Menu | F2: Debug",
                             -1, &r, DT_CENTER | DT_VCENTER, 0xFFFFC800);

    if (g_cheats.show_fps) {
        char buf[32]; snprintf(buf, sizeof(buf), "FPS: %d", g_fps);
        RECT fr = {0, 25, sw-10, 45};
        g_font_small->DrawTextA(NULL, buf, -1, &fr, DT_RIGHT | DT_VCENTER, 0xFF00FF64);
    }
}

void menu_render_debug(IDirect3DDevice9 *d) {
    if (!g_cheats.show_debug || !g_font_small) return;
    int sw = g_sw, sh = g_sh;
    int pw = 360, px = sw - pw - 10, py = 30;

    draw_rect(d, (float)px, (float)py, (float)pw, 340, 0xD8000000);
    draw_rect(d, (float)px, (float)py, (float)pw, 2, 0xFFFFC800);

    RECT tr = {px, py, px+pw, py+20};
    g_font_small->DrawTextA(NULL, "=== GAME STRINGS ===", -1, &tr, DT_CENTER, 0xFFFFC800);

    if (g_entity_count <= 0) {
        RECT er = {px, py+25, px+pw, py+45};
        g_font_small->DrawTextA(NULL, "No entities found", -1, &er, DT_CENTER, 0xFFE63C3C);
        return;
    }

    static int scroll = 0;
    static DWORD scroll_t = 0;
    if (GetTickCount() - scroll_t > 2000) { scroll_t = GetTickCount(); scroll++; if (scroll >= g_entity_count) scroll = 0; }

    int y = py + 25;
    for (int i = 0; i < 20 && y < sh - 20; i++) {
        int idx = (scroll + i) % g_entity_count;
        const char *str = g_entities[idx];
        if (!str) continue;
        char buf[56]; snprintf(buf, sizeof(buf), "%d: %s", idx, str);
        RECT sr = {px+6, y, px+pw-6, y+16};
        D3DCOLOR c = (i == 0) ? 0xFFFFC800 : 0xFFB4B4B4;
        g_font_small->DrawTextA(NULL, buf, -1, &sr, DT_LEFT | DT_VCENTER, c);
        y += 16;
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
