/*
 * Black Pearl Engine v4.0
 * D3D9 Proxy with EndScene hook
 * 
 * Compile: i686-w64-mingw32-g++ -shared -O2 -s -o d3d9.dll d3d9_proxy.c \
 *          -ld3d9 -ld3dx9 -lwinmm -static-libgcc -static-libstdc++
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#define MOD_NAME "Black Pearl Engine"
#define MOD_VER "v4.0"

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

/* Stud counter: SUB [0x00E41868], val at 0x00453664 and 0x00453684 */
#define STUD_SUB1_ADDR 0x00453664
#define STUD_SUB2_ADDR 0x00453684
static unsigned char g_stud_sub1_orig[10];
static unsigned char g_stud_sub2_orig[10];
static int g_stud_patched = 0;

static void apply_stud_patch(void) {
    if (g_stud_patched) return;
    /* Save originals */
    memcpy(g_stud_sub1_orig, (void*)STUD_SUB1_ADDR, 10);
    memcpy(g_stud_sub2_orig, (void*)STUD_SUB2_ADDR, 10);
    /* NOP the SUB instructions (10 bytes each) */
    unsigned char nops[10] = {0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90};
    patch_mem(STUD_SUB1_ADDR, nops, 10);
    patch_mem(STUD_SUB2_ADDR, nops, 10);
    g_stud_patched = 1;
    LOG("Infinite Studs: patched");
}

static void remove_stud_patch(void) {
    if (!g_stud_patched) return;
    patch_mem(STUD_SUB1_ADDR, g_stud_sub1_orig, 10);
    patch_mem(STUD_SUB2_ADDR, g_stud_sub2_orig, 10);
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

static void update_cheats(void) {
    if (g_cheats.infinite_studs) apply_stud_patch(); else remove_stud_patch();
    if (g_cheats.invincible) apply_health_patch(); else remove_health_patch();
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
 * Input
 * ================================================================ */
static int g_keys[256] = {0};

static int key_pressed(int vk) {
    SHORT s = GetAsyncKeyState(vk);
    int down = (s & 0x8000) != 0;
    int press = down && !g_keys[vk];
    g_keys[vk] = down;
    return press;
}

static void update_input(void) {
    if (key_pressed(VK_F1)) g_menu_open = !g_menu_open;
    if (key_pressed(VK_F2)) g_cheats.show_debug = !g_cheats.show_debug;
    if (!g_menu_open) return;
    if (key_pressed(VK_UP))   { g_sel--; if (g_sel < 0) g_sel = 0; }
    if (key_pressed(VK_DOWN)) { g_sel++; if (g_sel >= tabs[g_tab].count) g_sel = tabs[g_tab].count - 1; }
    if (key_pressed(VK_LEFT))  { g_tab = (g_tab - 1 + TAB_COUNT) % TAB_COUNT; g_sel = 0; }
    if (key_pressed(VK_RIGHT)) { g_tab = (g_tab + 1) % TAB_COUNT; g_sel = 0; }
    if (key_pressed(VK_RETURN)) {
        Item *it = &tabs[g_tab].items[g_sel];
        if (it->type == 0 && it->val) *(int*)it->val = !*(int*)it->val;
    }
    if (key_pressed(VK_LEFT)) {
        Item *it = &tabs[g_tab].items[g_sel];
        if (it->type == 1 && it->val) {
            int *v = (int*)it->val;
            if (*v >= 1000) *v -= 1000;
            else if (*v >= 100) *v -= 100;
            else if (*v >= 10) *v -= 10;
            else *v -= 1;
            if (*v < it->min) *v = it->min;
        }
    }
    if (key_pressed(VK_RIGHT)) {
        Item *it = &tabs[g_tab].items[g_sel];
        if (it->type == 1 && it->val) {
            int *v = (int*)it->val;
            if (*v >= 1000) *v += 1000;
            else if (*v >= 100) *v += 100;
            else if (*v >= 10) *v += 10;
            else *v += 1;
            if (*v > it->max) *v = it->max;
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
    Vertex v[4] = {
        {x,   y,   0, 1, c},
        {x+w, y,   0, 1, c},
        {x,   y+h, 0, 1, c},
        {x+w, y+h, 0, 1, c}
    };
    d->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    d->SetTexture(0, NULL);
    d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(Vertex));
}

static void render_menu(IDirect3DDevice9 *d) {
    if (!g_menu_open || !g_font || !g_font_small) return;
    int sw = g_sw, sh = g_sh;
    int mx = (sw - MENU_W) / 2, my = (sh - MENU_H) / 2;

    /* Fond noir semi-transparent */
    draw_rect(d, (float)mx, (float)my, (float)MENU_W, (float)MENU_H, 0xD8000000);
    /* Bordure dorée */
    draw_rect(d, (float)mx, (float)my, (float)MENU_W, 2, 0xFFFFC800);
    draw_rect(d, (float)mx, (float)my + MENU_H - 2, (float)MENU_W, 2, 0xFFFFC800);
    draw_rect(d, (float)mx, (float)my, 2, (float)MENU_H, 0xFFFFC800);
    draw_rect(d, (float)mx + MENU_W - 2, (float)my, 2, (float)MENU_H, 0xFFFFC800);

    /* Title */
    RECT r = {mx, my, mx+MENU_W, my+32};
    g_font->DrawTextA(NULL, MOD_NAME " " MOD_VER, -1, &r, DT_CENTER | DT_VCENTER, 0xFFFFC800);

    /* Tabs */
    float tw = (float)MENU_W / TAB_COUNT;
    for (int i = 0; i < TAB_COUNT; i++) {
        int tx = mx + (int)(i * tw);
        RECT tr = {tx, my+32, tx+(int)tw, my+56};
        D3DCOLOR c = (i == g_tab) ? 0xFFFFC800 : 0xFFC8C8C8;
        g_font_small->DrawTextA(NULL, tabs[i].name, -1, &tr, DT_CENTER | DT_VCENTER, c);
    }

    /* Items */
    Tab *cur = &tabs[g_tab];
    for (int i = 0; i < cur->count; i++) {
        int iy = my + 60 + i * ITEM_H;
        Item *it = &cur->items[i];
        D3DCOLOR c = (i == g_sel) ? 0xFFFFC800 : 0xFFE6E6E6;
        /* Fond de sélection */
        if (i == g_sel) {
            draw_rect(d, (float)mx + 4, (float)iy, (float)MENU_W - 8, (float)ITEM_H, 0x40FFC800);
        }
        RECT ir = {mx+12, iy, mx+MENU_W-12, iy+ITEM_H};
        g_font_small->DrawTextA(NULL, it->name, -1, &ir, DT_LEFT | DT_VCENTER, c);
        if (it->type == 0 && it->val) {
            int val = *(int*)it->val;
            RECT vr = {mx, iy, mx+MENU_W-12, iy+ITEM_H};
            g_font_small->DrawTextA(NULL, val ? "ON" : "OFF", -1, &vr, DT_RIGHT | DT_VCENTER,
                                      val ? 0xFF00E650 : 0xFFE63C3C);
        } else if (it->type == 1 && it->val) {
            int val = *(int*)it->val;
            char buf[32]; snprintf(buf, sizeof(buf), "%d", val);
            RECT vr = {mx, iy, mx+MENU_W-12, iy+ITEM_H};
            g_font_small->DrawTextA(NULL, buf, -1, &vr, DT_RIGHT | DT_VCENTER, 0xFF00A0FF);
        }
    }
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

/* ================================================================
 * EndScene Hook (Vtable Swap)
 * ================================================================ */
static HRESULT (WINAPI *orig_EndScene)(IDirect3DDevice9*) = NULL;
static void **g_fake_vt = NULL;

static HRESULT WINAPI hk_EndScene(IDirect3DDevice9 *d) {
    g_frame++;
    if (!g_dev) { g_dev = d; LOG("Device acquired"); }
    
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
    
    update_fps();
    update_input();
    update_cheats();
    render_overlay(d);
    render_debug(d);
    render_menu(d);
    
    return orig_EndScene(d);
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
    
    DWORD old;
    VirtualProtect(dev, sizeof(void*), PAGE_READWRITE, &old);
    *(void***)dev = g_fake_vt;
    VirtualProtect(dev, sizeof(void*), old, &old);
    
    LOG("Hooked! vtable copied: %d entries", 512);
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
            DisableThreadLibraryCalls(hInst);
            LOG("=== " MOD_NAME " " MOD_VER " ===");
            return TRUE;
        case DLL_PROCESS_DETACH:
            if (g_log) { fclose(g_log); g_log = NULL; }
            break;
    }
    return TRUE;
}
