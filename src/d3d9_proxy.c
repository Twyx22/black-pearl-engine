/*
 * Black Pearl Engine v4.0
 * D3D9 Proxy with EndScene hook
 * 
 * Compile: i686-w64-mingw32-g++ -shared -O2 -s -o d3d9.dll d3d9_proxy.c \
 *          -ld3d9 -ld3dx9 -lwinmm -static-libgcc -static-libstdc++
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#define MOD_NAME "Black Pearl Engine"
#define MOD_VER "v4.0"

static HINSTANCE g_hinst = NULL;

/* ================================================================
 * Logging
 * ================================================================ */
static FILE *g_log = NULL;
static void LOG(const char *fmt, ...) {
    if (!g_log) {
        g_log = fopen("bpe.log", "w");
        if (g_log) setvbuf(g_log, NULL, _IONBF, 0);
    }
    if (!g_log) return;
    va_list args;
    va_start(args, fmt);
    fprintf(g_log, "[%lu] ", GetTickCount());
    vfprintf(g_log, fmt, args);
    fprintf(g_log, "\n");
    va_end(args);
}

/* ================================================================
 * Cheats State
 * ================================================================ */
static struct {
    int invincible, infinite_studs, super_speed, super_jump;
    int moon_jump, time_freeze, noclip, show_debug, show_fps;
    int score_mult;
    int force_custom_studs;
    int custom_stud_value;
} g_cheats = {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 999999};

/* ================================================================
 * Memory Patcher
 * ================================================================ */
static void patch_mem(DWORD addr, const void *data, size_t len) {
    DWORD old;
    if (VirtualProtect((LPVOID)addr, len, PAGE_EXECUTE_READWRITE, &old)) {
        memcpy((void*)addr, data, len);
        VirtualProtect((LPVOID)addr, len, old, &old);
    }
}

/* Stud counter: dynamically find all SUB [0x00E41868] instructions */
#define STUD_COUNTER_ADDR 0x00E41868
#define MAX_STUD_PATCHES 32
static DWORD g_stud_patch_addrs[MAX_STUD_PATCHES];
static unsigned char g_stud_patch_origs[MAX_STUD_PATCHES][10];
static int g_stud_patch_count = 0;
static int g_stud_patched = 0;

static void scan_stud_subtractions(void) {
    if (g_stud_patch_count > 0) return;
    DWORD base = (DWORD)GetModuleHandleA(NULL);
    if (!base) return;
    
    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)base;
    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)(base + dos->e_lfanew);
    PIMAGE_SECTION_HEADER sec = IMAGE_FIRST_SECTION(nt);
    DWORD text_start = 0, text_size = 0;
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (memcmp(sec[i].Name, ".text", 5) == 0) {
            text_start = base + sec[i].VirtualAddress;
            text_size = sec[i].Misc.VirtualSize;
            break;
        }
    }
    if (!text_start || !text_size) return;
    
    unsigned char *code = (unsigned char*)text_start;
    for (DWORD i = 0; i < text_size - 10 && g_stud_patch_count < MAX_STUD_PATCHES; i++) {
        /* SUB DWORD PTR [disp32], imm32 = 81 /5 disp32 imm32 */
        if (code[i] == 0x81 && (code[i+1] & 0xC7) == 0x05) {
            DWORD disp = *(DWORD*)(code + i + 2);
            if (disp == STUD_COUNTER_ADDR) {
                g_stud_patch_addrs[g_stud_patch_count] = text_start + i;
                memcpy(g_stud_patch_origs[g_stud_patch_count], code + i, 10);
                g_stud_patch_count++;
            }
        }
        /* SUB DWORD PTR [disp32], imm8 = 83 /5 disp32 imm8 */
        if (code[i] == 0x83 && (code[i+1] & 0xC7) == 0x05) {
            DWORD disp = *(DWORD*)(code + i + 2);
            if (disp == STUD_COUNTER_ADDR) {
                g_stud_patch_addrs[g_stud_patch_count] = text_start + i;
                memcpy(g_stud_patch_origs[g_stud_patch_count], code + i, 7);
                g_stud_patch_count++;
            }
        }
    }
    LOG("Stud subtractions found: %d", g_stud_patch_count);
}

static void apply_stud_patch(void) {
    if (g_stud_patched) return;
    scan_stud_subtractions();
    unsigned char nops[10] = {0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90};
    for (int i = 0; i < g_stud_patch_count; i++) {
        int len = 10;
        /* Detect instruction length based on opcode */
        unsigned char *p = g_stud_patch_origs[i];
        if (p[0] == 0x83) len = 7;
        patch_mem(g_stud_patch_addrs[i], nops, len);
    }
    g_stud_patched = 1;
    LOG("Infinite Studs: patched %d locations", g_stud_patch_count);
}

static void remove_stud_patch(void) {
    if (!g_stud_patched) return;
    for (int i = 0; i < g_stud_patch_count; i++) {
        int len = 10;
        unsigned char *p = g_stud_patch_origs[i];
        if (p[0] == 0x83) len = 7;
        patch_mem(g_stud_patch_addrs[i], g_stud_patch_origs[i], len);
    }
    g_stud_patched = 0;
    LOG("Infinite Studs: unpatched");
}

/* Health: Search for DEC BYTE PTR [reg+0x864] patterns */
static DWORD g_health_addrs[16];
static unsigned char g_health_origs[16][4];
static int g_health_count = 0;
static int g_health_patched = 0;

static void scan_health_decrements(void) {
    if (g_health_count > 0) return;
    DWORD base = (DWORD)GetModuleHandleA(NULL);
    if (!base) return;
    
    /* Get .text section info */
    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)base;
    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)(base + dos->e_lfanew);
    PIMAGE_SECTION_HEADER sec = IMAGE_FIRST_SECTION(nt);
    DWORD text_start = 0, text_size = 0;
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (memcmp(sec[i].Name, ".text", 5) == 0) {
            text_start = base + sec[i].VirtualAddress;
            text_size = sec[i].Misc.VirtualSize;
            break;
        }
    }
    if (!text_start || !text_size) return;
    
    /* Search for DEC BYTE PTR [reg+0x864] and SUB BYTE PTR [reg+0x864], 1 */
    unsigned char *code = (unsigned char*)text_start;
    for (DWORD i = 0; i < text_size - 10 && g_health_count < 16; i++) {
        /* DEC BYTE PTR [reg+disp32] = FE /1 */
        if (code[i] == 0xFE && (code[i+1] & 0xC0) == 0x80) {
            DWORD disp = *(DWORD*)(code + i + 2);
            if (disp == 0x864) {
                g_health_addrs[g_health_count] = text_start + i;
                memcpy(g_health_origs[g_health_count], code + i, 4);
                g_health_count++;
            }
        }
        /* SUB BYTE PTR [reg+disp32], 1 = 80 /5 */
        if (code[i] == 0x80 && (code[i+1] & 0xC0) == 0x80 && code[i+6] == 0x01) {
            DWORD disp = *(DWORD*)(code + i + 2);
            if (disp == 0x864) {
                g_health_addrs[g_health_count] = text_start + i;
                memcpy(g_health_origs[g_health_count], code + i, 4);
                g_health_count++;
            }
        }
        /* SUB DWORD PTR [reg+disp32], 1 = 83 /5 */
        if (code[i] == 0x83 && (code[i+1] & 0xC0) == 0x80 && code[i+6] == 0x01) {
            DWORD disp = *(DWORD*)(code + i + 2);
            if (disp == 0x864) {
                g_health_addrs[g_health_count] = text_start + i;
                memcpy(g_health_origs[g_health_count], code + i, 4);
                g_health_count++;
            }
        }
    }
    LOG("Health decrements found: %d", g_health_count);
}

static void apply_health_patch(void) {
    if (g_health_patched) return;
    scan_health_decrements();
    unsigned char nop4[4] = {0x90, 0x90, 0x90, 0x90};
    for (int i = 0; i < g_health_count; i++) {
        patch_mem(g_health_addrs[i], nop4, 4);
    }
    g_health_patched = 1;
    LOG("Invincibility: patched %d locations", g_health_count);
}

static void remove_health_patch(void) {
    if (!g_health_patched) return;
    for (int i = 0; i < g_health_count; i++) {
        patch_mem(g_health_addrs[i], g_health_origs[i], 4);
    }
    g_health_patched = 0;
    LOG("Invincibility: unpatched");
}

/* ================================================================
 * Custom Stud Forcer
 * ================================================================ */
#define STUD_DISPLAY_ADDR 0x0359B660

static void force_custom_studs(void) {
    if (!g_cheats.force_custom_studs) return;
    DWORD addr = STUD_DISPLAY_ADDR;
    DWORD old;
    if (VirtualProtect((LPVOID)addr, 4, PAGE_READWRITE, &old)) {
        *(int*)addr = g_cheats.custom_stud_value;
        VirtualProtect((LPVOID)addr, 4, old, &old);
    }
}

/* Super Speed: modify walk/run speed constants */
#define SPEED_WALK_ADDR 0x00E24C50
#define SPEED_RUN_ADDR  0x00E24C54
static float g_speed_walk_orig = 3.0f;
static float g_speed_run_orig  = 3.5f;

static void apply_super_speed(void) {
    DWORD old;
    if (VirtualProtect((LPVOID)SPEED_WALK_ADDR, 4, PAGE_READWRITE, &old)) {
        *(float*)SPEED_WALK_ADDR = 300.0f;
        VirtualProtect((LPVOID)SPEED_WALK_ADDR, 4, old, &old);
    }
    if (VirtualProtect((LPVOID)SPEED_RUN_ADDR, 4, PAGE_READWRITE, &old)) {
        *(float*)SPEED_RUN_ADDR = 350.0f;
        VirtualProtect((LPVOID)SPEED_RUN_ADDR, 4, old, &old);
    }
}

static void remove_super_speed(void) {
    DWORD old;
    if (VirtualProtect((LPVOID)SPEED_WALK_ADDR, 4, PAGE_READWRITE, &old)) {
        *(float*)SPEED_WALK_ADDR = g_speed_walk_orig;
        VirtualProtect((LPVOID)SPEED_WALK_ADDR, 4, old, &old);
    }
    if (VirtualProtect((LPVOID)SPEED_RUN_ADDR, 4, PAGE_READWRITE, &old)) {
        *(float*)SPEED_RUN_ADDR = g_speed_run_orig;
        VirtualProtect((LPVOID)SPEED_RUN_ADDR, 4, old, &old);
    }
}

/* Time Freeze vars (defined in hooks section below) */
extern DWORD (WINAPI *real_GetTickCount)(void);
extern BOOL (WINAPI *real_QueryPerformanceCounter)(LARGE_INTEGER*);
extern DWORD g_freeze_tick;
extern LONGLONG g_freeze_perf;

static void update_cheats(void) {
    static int prev_time_freeze = 0;
    if (g_cheats.infinite_studs) apply_stud_patch(); else remove_stud_patch();
    if (g_cheats.invincible) apply_health_patch(); else remove_health_patch();
    if (g_cheats.super_speed) apply_super_speed(); else remove_super_speed();
    
    if (g_cheats.time_freeze && !prev_time_freeze) {
        g_freeze_tick = real_GetTickCount ? real_GetTickCount() : GetTickCount();
        LARGE_INTEGER li;
        if (real_QueryPerformanceCounter ? real_QueryPerformanceCounter(&li) : QueryPerformanceCounter(&li)) {
            g_freeze_perf = li.QuadPart;
        }
        LOG("Time Freeze ON: tick=%lu perf=%lld", g_freeze_tick, g_freeze_perf);
    }
    prev_time_freeze = g_cheats.time_freeze;
    
    force_custom_studs();
}

/* ================================================================
 * Entity Scanner
 * ================================================================ */
#define ENTITY_TABLE_OFF 0x00C8F400
static const char *g_entities[64];
static int g_entity_count = 0;

static void scan_entities(void) {
    DWORD base = (DWORD)GetModuleHandleA(NULL);
    if (!base) return;
    DWORD *table = (DWORD*)(base + ENTITY_TABLE_OFF);
    g_entity_count = 0;
    for (int i = 0; i < 1024 && g_entity_count < 64; i++) {
        const char *str = (const char*)table[i];
        if (!str || IsBadReadPtr(str, 4)) continue;
        if (str[0] >= 32 && str[0] < 127 && strlen(str) > 2 && strlen(str) < 48) {
            g_entities[g_entity_count++] = str;
        }
    }
    LOG("Entities: %d", g_entity_count);
}

/* ================================================================
 * Menu
 * ================================================================ */
static IDirect3DDevice9 *g_dev = NULL;
static ID3DXFont *g_font = NULL;
static ID3DXFont *g_font_small = NULL;
static int g_menu_open = 0, g_ready = 0, g_frame = 0;
static int g_sel = 0, g_tab = 0;

/* Input text box state (forward declared, defined after Item type) */
static void *g_editing_item_ptr = NULL;
static char g_edit_buf[32] = {0};
static int g_edit_len = 0;
static DWORD g_edit_cursor_t = 0;

#define MENU_W 480
#define MENU_H 440
#define ITEM_H 24

typedef struct { const char *name; int type; void *val; int min, max; } Item;
typedef struct { const char *name; Item *items; int count; } Tab;

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

static Tab tabs[] = {
    {"Health", health_items, 4},
    {"Studs", stud_items, 5},
    {"Fun", fun_items, 5},
    {"Visual", visual_items, 2},
};
#define TAB_COUNT 4

/* ================================================================
 * Input (uses internal keyboard state buffer updated by WH_KEYBOARD_LL)
 * ================================================================ */
static int g_keys[256] = {0};
static int g_key_states[256] = {0};

static int key_pressed(int vk) {
    int down = g_key_states[vk];
    int press = down && !g_keys[vk];
    g_keys[vk] = down;
    return press;
}

static int is_digit_key(int vk) {
    return (vk >= '0' && vk <= '9') || (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9);
}

static int vk_to_digit(int vk) {
    if (vk >= '0' && vk <= '9') return vk - '0';
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return vk - VK_NUMPAD0;
    return -1;
}

static void update_input(void) {
    if (key_pressed(VK_F1)) {
        g_menu_open = !g_menu_open;
        g_editing_item_ptr = NULL;
    }
    if (!g_menu_open) {
        if (key_pressed(VK_F2)) g_cheats.show_debug = !g_cheats.show_debug;
        return;
    }
    
    /* Input text box mode */
    if (g_editing_item_ptr) {
        if (key_pressed(VK_RETURN)) {
            /* Confirm */
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
            /* Cancel */
            g_editing_item_ptr = NULL;
            return;
        }
        if (key_pressed(VK_BACK)) {
            if (g_edit_len > 0) {
                g_edit_buf[--g_edit_len] = '\0';
            }
            return;
        }
        /* Check digit keys */
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
        return; /* Consume all other keys while editing */
    }
    
    /* Normal menu navigation */
    if (key_pressed(VK_UP))   { g_sel--; if (g_sel < 0) g_sel = 0; }
    if (key_pressed(VK_DOWN)) { g_sel++; if (g_sel >= tabs[g_tab].count) g_sel = tabs[g_tab].count - 1; }
    if (key_pressed(VK_LEFT))  { g_tab = (g_tab - 1 + TAB_COUNT) % TAB_COUNT; g_sel = 0; }
    if (key_pressed(VK_RIGHT)) { g_tab = (g_tab + 1) % TAB_COUNT; g_sel = 0; }
    if (key_pressed(VK_RETURN)) {
        Item *it = &tabs[g_tab].items[g_sel];
        if (it->type == 0 && it->val) {
            *(int*)it->val = !*(int*)it->val;
        } else if (it->type == 1 && it->val) {
            /* Enter edit mode */
            g_editing_item_ptr = it;
            g_edit_len = 0;
            g_edit_buf[0] = '\0';
            g_edit_cursor_t = GetTickCount();
        }
    }
}

/* ================================================================
 * Rendering
 * ================================================================ */
static int g_fps = 0, g_fc = 0;
static DWORD g_fps_t = 0;

static void update_fps(void) {
    g_fc++;
    DWORD now = GetTickCount();
    if (now - g_fps_t >= 1000) { g_fps = g_fc; g_fc = 0; g_fps_t = now; }
}

static int g_sw = 1280, g_sh = 720;

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

static void render_menu(IDirect3DDevice9 *d) {
    if (!g_menu_open || !g_font || !g_font_small) return;
    int sw = g_sw, sh = g_sh;
    int mx = (sw - MENU_W) / 2, my = (sh - MENU_H) / 2;

    /* Ombre portée (simulée avec rectangles noirs semi-transparents décalés) */
    for (int i = 1; i <= 4; i++) {
        draw_rect(d, (float)(mx + i), (float)(my + i), (float)MENU_W, (float)MENU_H, 0x40000000);
    }

    /* Fond principal avec bordure subtile */
    draw_rect(d, (float)mx, (float)my, (float)MENU_W, (float)MENU_H, 0xF0181818);
    draw_rect(d, (float)mx, (float)my, (float)MENU_W, 1, 0xFFFFC800);
    draw_rect(d, (float)mx, (float)my + MENU_H - 1, (float)MENU_W, 1, 0xFFFFC800);
    draw_rect(d, (float)mx, (float)my, 1, (float)MENU_H, 0xFFFFC800);
    draw_rect(d, (float)mx + MENU_W - 1, (float)my, 1, (float)MENU_H, 0xFFFFC800);

    /* Header bar avec dégradé doré/noir */
    draw_rect(d, (float)mx, (float)my, (float)MENU_W, 36, 0xFFFFC800);
    draw_rect(d, (float)mx, (float)my + 34, (float)MENU_W, 2, 0xFFD4A000);

    /* Title */
    RECT r = {mx, my, mx+MENU_W, my+36};
    g_font->DrawTextA(NULL, MOD_NAME " " MOD_VER, -1, &r, DT_CENTER | DT_VCENTER, 0xFF000000);

    /* Tabs */
    float tw = (float)MENU_W / TAB_COUNT;
    int tab_y = my + 40;
    for (int i = 0; i < TAB_COUNT; i++) {
        int tx = mx + (int)(i * tw);
        RECT tr = {tx, tab_y, tx+(int)tw, tab_y+24};
        if (i == g_tab) {
            /* Tab actif: fond doré */
            draw_rect(d, (float)(tx + 4), (float)(tab_y + 2), (float)(tw - 8), 22, 0x60FFC800);
            draw_rect(d, (float)(tx + 4), (float)(tab_y + 22), (float)(tw - 8), 2, 0xFFFFC800);
            g_font_small->DrawTextA(NULL, tabs[i].name, -1, &tr, DT_CENTER | DT_VCENTER, 0xFFFFC800);
        } else {
            g_font_small->DrawTextA(NULL, tabs[i].name, -1, &tr, DT_CENTER | DT_VCENTER, 0xFF909090);
        }
    }

    /* Ligne de séparation */
    draw_rect(d, (float)(mx + 8), (float)(tab_y + 26), (float)(MENU_W - 16), 1, 0xFF404040);

    /* Items */
    Tab *cur = &tabs[g_tab];
    int items_y = tab_y + 32;
    for (int i = 0; i < cur->count; i++) {
        int iy = items_y + i * (ITEM_H + 4);
        Item *it = &cur->items[i];
        
        /* Fond de ligne alterné */
        if (i % 2 == 0) {
            draw_rect(d, (float)(mx + 8), (float)iy, (float)(MENU_W - 16), (float)ITEM_H, 0x20FFFFFF);
        }
        
        /* Barre de sélection à gauche */
        if (i == g_sel) {
            draw_rect(d, (float)(mx + 4), (float)iy, 3, (float)ITEM_H, 0xFFFFC800);
            draw_rect(d, (float)(mx + 8), (float)iy, (float)(MENU_W - 16), (float)ITEM_H, 0x30FFC800);
        }
        
        /* Nom de l'item */
        RECT ir = {mx+18, iy, mx+MENU_W-80, iy+ITEM_H};
        D3DCOLOR text_c = (i == g_sel) ? 0xFFFFFFFF : 0xFFC8C8C8;
        g_font_small->DrawTextA(NULL, it->name, -1, &ir, DT_LEFT | DT_VCENTER, text_c);
        
        /* Valeur / Toggle */
        if (it->type == 0 && it->val) {
            int val = *(int*)it->val;
            int toggle_x = mx + MENU_W - 70;
            int toggle_y = iy + 4;
            int toggle_w = 50;
            int toggle_h = ITEM_H - 8;
            
            if (val) {
                /* Toggle ON - vert avec checkmark */
                draw_rect(d, (float)toggle_x, (float)toggle_y, (float)toggle_w, (float)toggle_h, 0xFF00AA00);
                RECT tr = {toggle_x, toggle_y, toggle_x+toggle_w, toggle_y+toggle_h};
                g_font_small->DrawTextA(NULL, "ON", -1, &tr, DT_CENTER | DT_VCENTER, 0xFFFFFFFF);
            } else {
                /* Toggle OFF - gris rougeâtre */
                draw_rect(d, (float)toggle_x, (float)toggle_y, (float)toggle_w, (float)toggle_h, 0xFF444444);
                RECT tr = {toggle_x, toggle_y, toggle_x+toggle_w, toggle_y+toggle_h};
                g_font_small->DrawTextA(NULL, "OFF", -1, &tr, DT_CENTER | DT_VCENTER, 0xFF888888);
            }
        } else if (it->type == 1 && it->val) {
            if (g_editing_item_ptr == it) {
                /* Edit mode: boîte de saisie stylisée */
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
                /* Valeur numérique avec fond */
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

    /* Footer avec instructions */
    int foot_y = my + MENU_H - 24;
    draw_rect(d, (float)(mx + 8), (float)(foot_y - 4), (float)(MENU_W - 16), 1, 0xFF404040);
    RECT fr = {mx, foot_y, mx+MENU_W, foot_y+20};
    g_font_small->DrawTextA(NULL, "ENTER: Toggle/Edit  |  ARROWS: Navigate  |  ESC: Cancel Edit", -1, &fr, DT_CENTER | DT_VCENTER, 0xFF808080);
}

static void render_overlay(IDirect3DDevice9 *d) {
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

static void render_debug(IDirect3DDevice9 *d) {
    if (!g_cheats.show_debug || !g_font_small) return;
    int sw = g_sw, sh = g_sh;
    int pw = 360, px = sw - pw - 10, py = 30;

    /* Fond debug */
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

/* Forward declaration */
static void install_input_hooks(void);

/* ================================================================
 * Window Subclassing (blocks keyboard messages BEFORE game sees them)
 * ================================================================ */
static HWND g_game_hwnd = NULL;
static WNDPROC g_orig_wndproc = NULL;

static int g_wndproc_blocked = 0;

static LRESULT CALLBACK hk_wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_menu_open) {
        /* Block ALL messages when menu is open to be sure */
        if (msg == WM_KEYDOWN || msg == WM_KEYUP ||
            msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP ||
            msg == WM_CHAR || msg == WM_DEADCHAR) {
            g_wndproc_blocked++;
            if (g_wndproc_blocked <= 5) {
                LOG("WndProc blocked msg=%04X vk=%d", msg, (int)wParam);
            }
            return 0; /* Eat the message */
        }
    }
    if (g_orig_wndproc) {
        return CallWindowProc(g_orig_wndproc, hwnd, msg, wParam, lParam);
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static void hook_window(HWND hwnd) {
    if (!hwnd || g_orig_wndproc) return;
    g_orig_wndproc = (WNDPROC)GetWindowLongPtr(hwnd, GWLP_WNDPROC);
    if (g_orig_wndproc) {
        SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)hk_wndproc);
        LOG("Window subclassed: hwnd=%p oldproc=%p", hwnd, g_orig_wndproc);
        /* Test: send a test message to verify subclassing works */
        PostMessage(hwnd, WM_NULL, 0, 0);
    }
}

/* ================================================================
 * EndScene Hook (Vtable Swap)
 * ================================================================ */
static HRESULT (WINAPI *orig_EndScene)(IDirect3DDevice9*) = NULL;
static void **g_fake_vt = NULL;

static HRESULT WINAPI hk_EndScene(IDirect3DDevice9 *d) {
    g_frame++;
    if (!g_dev) { g_dev = d; LOG("Device acquired"); }
    
    D3DVIEWPORT9 vp;
    if (SUCCEEDED(d->GetViewport(&vp))) {
        g_sw = vp.Width;
        g_sh = vp.Height;
    }
    
    if (!g_ready && g_frame > 90) {
        g_ready = 1;
        LOG("Ready at frame %d", g_frame);
    }
    
    if (g_ready && !g_font) {
        D3DXCreateFontA(d, 16, 0, FW_BOLD, 0, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                       "Arial", &g_font);
        D3DXCreateFontA(d, 13, 0, FW_NORMAL, 0, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                       "Arial", &g_font_small);
        if (g_font) LOG("Fonts OK");
    }
    if (g_ready && !g_entity_count) scan_entities();
    
    /* Get game window handle and subclass it */
    if (!g_game_hwnd && g_dev) {
        D3DDEVICE_CREATION_PARAMETERS cp;
        if (SUCCEEDED(g_dev->GetCreationParameters(&cp))) {
            g_game_hwnd = cp.hFocusWindow;
            if (!g_game_hwnd) g_game_hwnd = cp.hFocusWindow;
            if (g_game_hwnd) {
                hook_window(g_game_hwnd);
            }
        }
    }
    
    /* Install input hooks with retry - game modules might load late */
    if (g_frame > 10 && g_frame % 120 == 0) {
        install_input_hooks();
    }
    
    update_fps();
    update_input();
    update_cheats();
    render_overlay(d);
    render_debug(d);
    render_menu(d);
    
    return orig_EndScene(d);
}

static HRESULT (WINAPI *orig_Reset)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*) = NULL;

static HRESULT WINAPI hk_Reset(IDirect3DDevice9 *d, D3DPRESENT_PARAMETERS *pp) {
    if (g_font) { g_font->Release(); g_font = NULL; }
    if (g_font_small) { g_font_small->Release(); g_font_small = NULL; }
    
    g_sw = pp->BackBufferWidth;
    g_sh = pp->BackBufferHeight;
    LOG("Reset: %dx%d windowed=%d", g_sw, g_sh, pp->Windowed);
    
    HRESULT hr = orig_Reset(d, pp);
    return hr;
}

static void hook_device(IDirect3DDevice9 *dev) {
    if (!dev || orig_EndScene) return;
    void **vt = *(void***)dev;
    if (!vt) return;
    
    /* Copy 512 vtable entries for DXVK compatibility */
    size_t sz = 512 * sizeof(void*);
    g_fake_vt = (void**)VirtualAlloc(NULL, sz, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!g_fake_vt) return;
    
    memcpy(g_fake_vt, vt, sz);
    orig_EndScene = (HRESULT (WINAPI *)(IDirect3DDevice9*))g_fake_vt[42];
    g_fake_vt[42] = (void*)hk_EndScene;
    
    orig_Reset = (HRESULT (WINAPI *)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*))g_fake_vt[16];
    g_fake_vt[16] = (void*)hk_Reset;
    
    DWORD old;
    VirtualProtect(dev, sizeof(void*), PAGE_READWRITE, &old);
    *(void***)dev = g_fake_vt;
    VirtualProtect(dev, sizeof(void*), old, &old);
    
    LOG("Hooked! vtable copied: %d entries", 512);
}

/* ================================================================
 * Input Blocker (PeekMessageA Hook)
 * ================================================================ */
static BOOL (WINAPI *real_PeekMessageA)(LPMSG, HWND, UINT, UINT, UINT) = NULL;

static BOOL WINAPI hk_PeekMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin,
                                    UINT wMsgFilterMax, UINT wRemoveMsg) {
    BOOL ret = real_PeekMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
    if (!ret || !g_menu_open) return ret;
    
    /* Block ALL keyboard messages when menu is open */
    if (lpMsg->message == WM_KEYDOWN || lpMsg->message == WM_KEYUP ||
        lpMsg->message == WM_SYSKEYDOWN || lpMsg->message == WM_SYSKEYUP ||
        lpMsg->message == WM_CHAR || lpMsg->message == WM_DEADCHAR) {
        /* Always consume the message from queue */
        if (wRemoveMsg & PM_REMOVE) {
            lpMsg->message = WM_NULL;
            lpMsg->wParam = 0;
            lpMsg->lParam = 0;
        } else {
            /* If PM_NOREMOVE, remove it for real so game never sees it */
            MSG dummy;
            real_PeekMessageA(&dummy, hWnd, lpMsg->message, lpMsg->message, PM_REMOVE);
        }
        return FALSE;
    }
    return ret;
}

static void hook_peekmessage(void) {
    if (real_PeekMessageA) return;
    HMODULE hUser32 = GetModuleHandleA("user32.dll");
    if (!hUser32) return;
    real_PeekMessageA = (BOOL (WINAPI *)(LPMSG, HWND, UINT, UINT, UINT))
        GetProcAddress(hUser32, "PeekMessageA");
    if (!real_PeekMessageA) return;
    
    /* IAT hook on the game's PeekMessageA import */
    DWORD base = (DWORD)GetModuleHandleA(NULL);
    if (!base) return;
    
    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)base;
    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)(base + dos->e_lfanew);
    PIMAGE_IMPORT_DESCRIPTOR import = (PIMAGE_IMPORT_DESCRIPTOR)(base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    
    while (import->Name) {
        const char *name = (const char*)(base + import->Name);
        if (_stricmp(name, "USER32.dll") == 0 || _stricmp(name, "user32.dll") == 0) {
            PIMAGE_THUNK_DATA thunk = (PIMAGE_THUNK_DATA)(base + import->FirstThunk);
            while (thunk->u1.Function) {
                if ((DWORD)thunk->u1.Function == (DWORD)real_PeekMessageA) {
                    DWORD old;
                    if (VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &old)) {
                        thunk->u1.Function = (ULONG_PTR)hk_PeekMessageA;
                        VirtualProtect(&thunk->u1.Function, sizeof(void*), old, &old);
                        LOG("PeekMessageA hooked");
                        return;
                    }
                }
                thunk++;
            }
        }
        import++;
    }
}

/* ================================================================
 * GetMessageA Hook (some games use GetMessage instead of PeekMessage)
 * ================================================================ */
static BOOL (WINAPI *real_GetMessageA)(LPMSG, HWND, UINT, UINT) = NULL;

static BOOL WINAPI hk_GetMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax) {
    BOOL ret = real_GetMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
    if (!ret || !g_menu_open) return ret;
    
    /* Block ALL keyboard messages when menu is open */
    if (lpMsg->message == WM_KEYDOWN || lpMsg->message == WM_KEYUP ||
        lpMsg->message == WM_SYSKEYDOWN || lpMsg->message == WM_SYSKEYUP ||
        lpMsg->message == WM_CHAR || lpMsg->message == WM_DEADCHAR) {
        lpMsg->message = WM_NULL;
        lpMsg->wParam = 0;
        lpMsg->lParam = 0;
        return TRUE; /* Pretend we got a message (WM_NULL) */
    }
    return ret;
}

/* ================================================================
 * Time Freeze (GetTickCount + QueryPerformanceCounter hooks)
 * ================================================================ */
DWORD (WINAPI *real_GetTickCount)(void) = NULL;
BOOL (WINAPI *real_QueryPerformanceCounter)(LARGE_INTEGER*) = NULL;
DWORD g_freeze_tick = 0;
LONGLONG g_freeze_perf = 0;
int g_time_hooks_installed = 0;

static DWORD WINAPI hk_GetTickCount(void) {
    if (g_cheats.time_freeze) return g_freeze_tick;
    return real_GetTickCount();
}

static BOOL WINAPI hk_QueryPerformanceCounter(LARGE_INTEGER *lpCount) {
    if (g_cheats.time_freeze && lpCount) {
        lpCount->QuadPart = g_freeze_perf;
        return TRUE;
    }
    return real_QueryPerformanceCounter(lpCount);
}

/* Hook IAT entry in a specific module */
static int hook_iat_in_module(HMODULE hMod, const char *dll_name, void *real_fn, void *hook_fn, const char *func_name) {
    if (!hMod || !real_fn) return 0;
    DWORD base = (DWORD)hMod;
    
    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    
    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    
    DWORD import_rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!import_rva) return 0;
    
    PIMAGE_IMPORT_DESCRIPTOR import = (PIMAGE_IMPORT_DESCRIPTOR)(base + import_rva);
    int hooked = 0;
    
    while (import->Name) {
        const char *name = (const char*)(base + import->Name);
        if (_stricmp(name, dll_name) == 0) {
            PIMAGE_THUNK_DATA thunk = (PIMAGE_THUNK_DATA)(base + import->FirstThunk);
            while (thunk->u1.Function) {
                if ((DWORD)thunk->u1.Function == (DWORD)real_fn) {
                    DWORD old;
                    if (VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &old)) {
                        thunk->u1.Function = (ULONG_PTR)hook_fn;
                        VirtualProtect(&thunk->u1.Function, sizeof(void*), old, &old);
                        hooked++;
                    }
                }
                thunk++;
            }
        }
        import++;
    }
    return hooked;
}

/* Hook IAT function across ALL loaded modules */
static void hook_iat_function_all(const char *dll_name, const char *func_name, void *hook_fn, void **real_fn_out) {
    /* Get real function address once */
    if (!*real_fn_out) {
        HMODULE hMod = GetModuleHandleA(dll_name);
        if (hMod) {
            *real_fn_out = (void*)GetProcAddress(hMod, func_name);
        }
    }
    if (!*real_fn_out) return;
    
    /* Try hooking in main module */
    HMODULE hMain = GetModuleHandleA(NULL);
    int total = hook_iat_in_module(hMain, dll_name, *real_fn_out, hook_fn, func_name);
    
    /* Also try in all loaded modules - the game might call it from a DLL */
    HMODULE mods[256];
    DWORD needed;
    HANDLE hProc = GetCurrentProcess();
    if (EnumProcessModules(hProc, mods, sizeof(mods), &needed)) {
        int count = needed / sizeof(HMODULE);
        for (int i = 0; i < count; i++) {
            if (mods[i] != hMain) {
                total += hook_iat_in_module(mods[i], dll_name, *real_fn_out, hook_fn, func_name);
            }
        }
    }
    
    if (total > 0) {
        LOG("%s hooked in %d location(s)", func_name, total);
    }
}

/* Legacy single-module hook (kept for backward compat) */
static void hook_iat_function(const char *dll_name, const char *func_name, void *hook_fn, void **real_fn) {
    if (*real_fn) return;
    HMODULE hMod = GetModuleHandleA(dll_name);
    if (!hMod) return;
    *real_fn = (void*)GetProcAddress(hMod, func_name);
    if (!*real_fn) return;
    
    DWORD base = (DWORD)GetModuleHandleA(NULL);
    if (!base) return;
    
    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)base;
    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)(base + dos->e_lfanew);
    PIMAGE_IMPORT_DESCRIPTOR import = (PIMAGE_IMPORT_DESCRIPTOR)(base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    
    while (import->Name) {
        const char *name = (const char*)(base + import->Name);
        if (_stricmp(name, dll_name) == 0) {
            PIMAGE_THUNK_DATA thunk = (PIMAGE_THUNK_DATA)(base + import->FirstThunk);
            while (thunk->u1.Function) {
                if ((DWORD)thunk->u1.Function == (DWORD)*real_fn) {
                    DWORD old;
                    if (VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &old)) {
                        thunk->u1.Function = (ULONG_PTR)hook_fn;
                        VirtualProtect(&thunk->u1.Function, sizeof(void*), old, &old);
                        LOG("%s hooked", func_name);
                    }
                }
                thunk++;
            }
        }
        import++;
    }
}

static void install_time_hooks(void) {
    if (g_time_hooks_installed) return;
    hook_iat_function("kernel32.dll", "GetTickCount", (void*)hk_GetTickCount, (void**)&real_GetTickCount);
    hook_iat_function("kernel32.dll", "QueryPerformanceCounter", (void*)hk_QueryPerformanceCounter, (void**)&real_QueryPerformanceCounter);
    g_time_hooks_installed = 1;
}

/* ================================================================
 * DirectInput8 Hook (game uses DINPUT8.dll for keyboard input)
 * ================================================================ */
static int g_hook_log_count = 0;

typedef HRESULT (WINAPI *DirectInput8Create_t)(HINSTANCE, DWORD, REFIID, LPVOID*, void*);
static DirectInput8Create_t real_DirectInput8Create = NULL;

/* IDirectInput8 vtable indices */
#define DI8_VT_CREATE_DEVICE 3
#define DI8_VT_RELEASE 2

/* IDirectInputDevice8 vtable indices */
#define DIDEV_VT_GET_DEVICE_STATE 9
#define DIDEV_VT_ACQUIRE 7
#define DIDEV_VT_UNACQUIRE 8
#define DIDEV_VT_SET_COOPERATIVE_LEVEL 13

static void **g_fake_di8_vt = NULL;
static void **g_real_di8_vt = NULL;
static HRESULT (WINAPI *orig_CreateDevice_di)(void*, REFGUID, void**, void*) = NULL;

static void **g_fake_kb_vt = NULL;
static void **g_real_kb_vt = NULL;
static void *g_kb_device = NULL;
static HRESULT (WINAPI *orig_GetDeviceState)(void*, DWORD, LPVOID) = NULL;
static HRESULT (WINAPI *orig_Acquire)(void*) = NULL;
static HRESULT (WINAPI *orig_Unacquire)(void*) = NULL;

static HRESULT WINAPI hk_GetDeviceState(void *dev, DWORD size, LPVOID data) {
    if (g_menu_open && data && size == 256) {
        if (g_hook_log_count < 15) {
            g_hook_log_count++;
            LOG("DirectInput GetDeviceState blocked");
        }
        memset(data, 0, 256);
        return 0; /* DI_OK */
    }
    return orig_GetDeviceState(dev, size, data);
}

static HRESULT WINAPI hk_Acquire(void *dev) {
    if (g_menu_open) {
        if (g_hook_log_count < 15) {
            g_hook_log_count++;
            LOG("DirectInput Acquire blocked");
        }
        return 0; /* DI_OK - pretend we acquired */
    }
    return orig_Acquire(dev);
}

static HRESULT WINAPI hk_Unacquire(void *dev) {
    return orig_Unacquire(dev);
}

static HRESULT WINAPI hk_CreateDevice_di(void *di, REFGUID rguid, void **ppDevice, void *unk) {
    HRESULT hr = orig_CreateDevice_di(di, rguid, ppDevice, unk);
    if (SUCCEEDED(hr) && ppDevice && *ppDevice) {
        /* Check if it's a keyboard device */
        /* GUID_SysKeyboard = {0x6F1D2B61,0xD5A0,0x11CF,{0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00}} */
        static const GUID GUID_SysKeyboard = {0x6F1D2B61,0xD5A0,0x11CF,{0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00}};
        if (memcmp(&rguid, &GUID_SysKeyboard, sizeof(GUID)) == 0) {
            g_kb_device = *ppDevice;
            void **vt = *(void***)*ppDevice;
            if (vt && !g_fake_kb_vt) {
                g_real_kb_vt = vt;
                size_t sz = 32 * sizeof(void*);
                g_fake_kb_vt = (void**)VirtualAlloc(NULL, sz, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
                if (g_fake_kb_vt) {
                    memcpy(g_fake_kb_vt, vt, sz);
                    orig_GetDeviceState = (HRESULT (WINAPI *)(void*, DWORD, LPVOID))g_fake_kb_vt[DIDEV_VT_GET_DEVICE_STATE];
                    orig_Acquire = (HRESULT (WINAPI *)(void*))g_fake_kb_vt[DIDEV_VT_ACQUIRE];
                    orig_Unacquire = (HRESULT (WINAPI *)(void*))g_fake_kb_vt[DIDEV_VT_UNACQUIRE];
                    g_fake_kb_vt[DIDEV_VT_GET_DEVICE_STATE] = (void*)hk_GetDeviceState;
                    g_fake_kb_vt[DIDEV_VT_ACQUIRE] = (void*)hk_Acquire;
                    g_fake_kb_vt[DIDEV_VT_UNACQUIRE] = (void*)hk_Unacquire;
                    DWORD old;
                    VirtualProtect(*ppDevice, sizeof(void*), PAGE_READWRITE, &old);
                    *(void***)*ppDevice = g_fake_kb_vt;
                    VirtualProtect(*ppDevice, sizeof(void*), old, &old);
                    LOG("DirectInput keyboard device hooked");
                }
            }
        }
    }
    return hr;
}

static HRESULT WINAPI hk_DirectInput8Create(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID *ppvOut, void *punkOuter) {
    HRESULT hr = real_DirectInput8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter);
    if (SUCCEEDED(hr) && ppvOut && *ppvOut) {
        void **vt = *(void***)*ppvOut;
        if (vt && !g_fake_di8_vt) {
            g_real_di8_vt = vt;
            size_t sz = 16 * sizeof(void*);
            g_fake_di8_vt = (void**)VirtualAlloc(NULL, sz, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
            if (g_fake_di8_vt) {
                memcpy(g_fake_di8_vt, vt, sz);
                orig_CreateDevice_di = (HRESULT (WINAPI *)(void*, REFGUID, void**, void*))g_fake_di8_vt[DI8_VT_CREATE_DEVICE];
                g_fake_di8_vt[DI8_VT_CREATE_DEVICE] = (void*)hk_CreateDevice_di;
                DWORD old;
                VirtualProtect(ppvOut, sizeof(void*), PAGE_READWRITE, &old);
                *(void***)ppvOut = g_fake_di8_vt;
                VirtualProtect(ppvOut, sizeof(void*), old, &old);
                LOG("DirectInput8 interface hooked");
            }
        }
    }
    return hr;
}

/* ================================================================
 * Keyboard Input Blocking (WH_KEYBOARD + WH_KEYBOARD_LL hooks)
 * ================================================================ */
static HHOOK g_kb_hook = NULL;
static HHOOK g_kb_ll_hook = NULL;
static int g_input_hooks_installed = 0;
static int g_input_hook_attempts = 0;

static LRESULT CALLBACK hk_keyboard_proc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && g_menu_open) {
        return 1; /* Non-zero = message eaten, game never sees it */
    }
    return CallNextHookEx(g_kb_hook, nCode, wParam, lParam);
}

static LRESULT CALLBACK hk_keyboard_ll_proc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        KBDLLHOOKSTRUCT *p = (KBDLLHOOKSTRUCT*)lParam;
        int vk = (int)p->vkCode;
        
        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            if (vk >= 0 && vk < 256) g_key_states[vk] = 1;
        } else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
            if (vk >= 0 && vk < 256) g_key_states[vk] = 0;
        }
        
        if (g_menu_open) {
            return 1; /* Eat the input at driver level - game never sees it */
        }
    }
    return CallNextHookEx(g_kb_ll_hook, nCode, wParam, lParam);
}

static void install_input_hooks(void) {
    if (g_input_hooks_installed) return;
    if (g_input_hook_attempts > 5) return; /* Give up after 5 retries */
    g_input_hook_attempts++;
    
    LOG("Installing input hooks (attempt %d)...", g_input_hook_attempts);
    
    /* NOTE: We do NOT hook GetAsyncKeyState/GetKeyState/GetKeyboardState because
     * the game uses DirectInput8 (DINPUT8.dll) for keyboard input, not Windows APIs.
     * Hooking Windows APIs would only block our own menu navigation.
     * Instead, we use WH_KEYBOARD_LL which intercepts input at the driver level,
     * before it reaches DirectInput or any other API. */
    
    /* Method 1: DirectInput8 hook (fallback in case device is created after our hooks) */
    hook_iat_function_all("dinput8.dll", "DirectInput8Create", (void*)hk_DirectInput8Create, (void**)&real_DirectInput8Create);
    
    /* Method 2: WH_KEYBOARD hook - local thread (secondary fallback) */
    if (!g_kb_hook && g_hinst) {
        DWORD tid = GetCurrentThreadId();
        g_kb_hook = SetWindowsHookEx(WH_KEYBOARD, hk_keyboard_proc, g_hinst, tid);
        if (g_kb_hook) {
            LOG("WH_KEYBOARD hook installed (tid=%lu)", tid);
        } else {
            LOG("WH_KEYBOARD hook failed (err=%lu)", GetLastError());
        }
    }
    
    /* Method 3: WH_KEYBOARD_LL hook - global low-level (PRIMARY method)
     * This intercepts keyboard events at the driver level, before DirectInput
     * or any game code sees them. It also updates our internal key state buffer
     * which the menu reads for navigation. */
    if (!g_kb_ll_hook && g_hinst) {
        g_kb_ll_hook = SetWindowsHookEx(WH_KEYBOARD_LL, hk_keyboard_ll_proc, g_hinst, 0);
        if (g_kb_ll_hook) {
            LOG("WH_KEYBOARD_LL hook installed (global)");
        } else {
            LOG("WH_KEYBOARD_LL hook failed (err=%lu)", GetLastError());
        }
    }
    
    g_input_hooks_installed = 1;
    LOG("Input hooks installation complete");
}

/* ================================================================
 * D3D9 Proxy
 * ================================================================ */
static IDirect3D9* (WINAPI *real_D3DCreate9)(UINT) = NULL;
static HRESULT (WINAPI *real_CreateDevice)(IDirect3D9*, UINT, D3DDEVTYPE, HWND,
                                            DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**) = NULL;

static HRESULT WINAPI hk_CreateDevice(IDirect3D9 *d3d, UINT Adapter, D3DDEVTYPE Type,
                                       HWND hWnd, DWORD Flags, D3DPRESENT_PARAMETERS *pp,
                                       IDirect3DDevice9 **ppDev) {
    g_sw = pp->BackBufferWidth; g_sh = pp->BackBufferHeight;
    LOG("CreateDevice: %dx%d windowed=%d", g_sw, g_sh, pp->Windowed);
    HRESULT hr = real_CreateDevice(d3d, Adapter, Type, hWnd, Flags, pp, ppDev);
    if (SUCCEEDED(hr) && ppDev && *ppDev) {
        LOG("Device: %p", *ppDev);
        hook_device(*ppDev);
    }
    return hr;
}

extern "C" __declspec(dllexport) IDirect3D9* WINAPI Direct3DCreate9(UINT sdk) {
    LOG("Direct3DCreate9(%d)", sdk);
    
    if (!real_D3DCreate9) {
        char path[MAX_PATH];
        GetSystemDirectoryA(path, MAX_PATH);
        strcat(path, "\\d3d9.dll");
        HMODULE h = LoadLibraryA(path);
        if (h) real_D3DCreate9 = (IDirect3D9* (WINAPI *)(UINT))GetProcAddress(h, "Direct3DCreate9");
    }
    if (!real_D3DCreate9) return NULL;
    
    IDirect3D9 *d3d = real_D3DCreate9(sdk);
    if (!d3d) return NULL;
    
    /* Hook CreateDevice */
    void **vt = *(void***)d3d;
    real_CreateDevice = (HRESULT (WINAPI *)(IDirect3D9*, UINT, D3DDEVTYPE, HWND,
                                              DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**))vt[16];
    DWORD old;
    VirtualProtect(&vt[16], sizeof(void*), PAGE_READWRITE, &old);
    vt[16] = (void*)hk_CreateDevice;
    VirtualProtect(&vt[16], sizeof(void*), old, &old);
    
    return d3d;
}

/* Forward other exports */
extern "C" __declspec(dllexport) HRESULT WINAPI Direct3DCreate9Ex(UINT sdk, IDirect3D9Ex **ex) {
    typedef HRESULT (WINAPI *fn)(UINT, IDirect3D9Ex**);
    fn f = NULL;
    char path[MAX_PATH]; GetSystemDirectoryA(path, MAX_PATH); strcat(path, "\\d3d9.dll");
    HMODULE h = GetModuleHandleA(path); if (h) f = (fn)GetProcAddress(h, "Direct3DCreate9Ex");
    return f ? f(sdk, ex) : E_NOTIMPL;
}

extern "C" __declspec(dllexport) int WINAPI D3DPERF_BeginEvent(DWORD c, const WCHAR *n) {
    typedef int (WINAPI *fn)(DWORD, const WCHAR*);
    fn f = NULL;
    char path[MAX_PATH]; GetSystemDirectoryA(path, MAX_PATH); strcat(path, "\\d3d9.dll");
    HMODULE h = GetModuleHandleA(path); if (h) f = (fn)GetProcAddress(h, "D3DPERF_BeginEvent");
    return f ? f(c, n) : 0;
}

extern "C" __declspec(dllexport) int WINAPI D3DPERF_EndEvent(void) {
    typedef int (WINAPI *fn)(void);
    fn f = NULL;
    char path[MAX_PATH]; GetSystemDirectoryA(path, MAX_PATH); strcat(path, "\\d3d9.dll");
    HMODULE h = GetModuleHandleA(path); if (h) f = (fn)GetProcAddress(h, "D3DPERF_EndEvent");
    return f ? f() : 0;
}

/* ================================================================
 * DLL Main
 * ================================================================ */
BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID reserved) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            g_hinst = hInst;
            DisableThreadLibraryCalls(hInst);
            LOG("=== " MOD_NAME " " MOD_VER " ===");
            hook_peekmessage();
            install_time_hooks();
            /* Input hooks installed lazily in EndScene when game is fully loaded */
            return TRUE;
        case DLL_PROCESS_DETACH:
            if (g_kb_hook) { UnhookWindowsHookEx(g_kb_hook); g_kb_hook = NULL; }
            if (g_log) { fclose(g_log); g_log = NULL; }
            break;
    }
    return TRUE;
}
