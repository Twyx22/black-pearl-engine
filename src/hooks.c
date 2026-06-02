#include "hooks.h"
#include "utils.h"
#include "cheats.h"
#include "menu.h"
#include "input.h"
#include "config.h"
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx9.h"
#include <MinHook.h>

HWND g_game_hwnd = NULL;
int g_imgui_ready = 0;
static WNDPROC g_orig_wndproc = NULL;
static HRESULT (WINAPI *orig_EndScene)(IDirect3DDevice9*) = NULL;
static HRESULT (WINAPI *orig_Present)(IDirect3DDevice9*, CONST RECT*, CONST RECT*, HWND, CONST RGNDATA*) = NULL;
static void **g_fake_vt = NULL;
static HRESULT (WINAPI *orig_Reset)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*) = NULL;

static int g_time_hooks_installed = 0;

DWORD (WINAPI *real_GetTickCount)(void) = NULL;
BOOL (WINAPI *real_QueryPerformanceCounter)(LARGE_INTEGER*) = NULL;
DWORD g_freeze_tick = 0;
LONGLONG g_freeze_perf = 0;

void time_freeze_snapshot(void) {
    g_freeze_tick = real_GetTickCount ? real_GetTickCount() : GetTickCount();
    LARGE_INTEGER li;
    if (real_QueryPerformanceCounter ? real_QueryPerformanceCounter(&li) : QueryPerformanceCounter(&li)) {
        g_freeze_perf = li.QuadPart;
    }
}

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

void install_time_hooks(void) {
    if (g_time_hooks_installed) return;
    if (MH_CreateHookApi(L"kernel32.dll", "GetTickCount", (LPVOID)hk_GetTickCount, (void**)&real_GetTickCount) != MH_OK) {
        LOG("GetTickCount hook failed");
    }
    if (MH_CreateHookApi(L"kernel32.dll", "QueryPerformanceCounter", (LPVOID)hk_QueryPerformanceCounter, (void**)&real_QueryPerformanceCounter) != MH_OK) {
        LOG("QueryPerformanceCounter hook failed");
    }
    g_time_hooks_installed = 1;
    LOG("Time hooks installed via MinHook");
}

// ImGui's WndProcHandler: for keyboard (WM_KEYDOWN/UP) it records keys internally
// and returns 0, so it never blocks them. For mouse/capture messages it may return
// non-zero to indicate "ImGui consumed this".
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static LRESULT CALLBACK hk_wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // 1. Track all keyboard state. Swallow F1 so the game never sees it.
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
        int vk = (int)wParam;
        if (vk >= 0 && vk < 256) {
            g_key_states[vk] = 1;
            if (vk == VK_F1) {
                LOG("WndProc: F1 KEYDOWN received (lParam=%08X)", (unsigned)lParam);
                return 0;                 // swallow F1 — menu_update_input will toggle
            }
        }
    } else if (msg == WM_KEYUP || msg == WM_SYSKEYUP) {
        int vk = (int)wParam;
        if (vk >= 0 && vk < 256) g_key_states[vk] = 0;
    }

    // 2. Let ImGui see the message (for its internal state / widgets).
    //    It returns TRUE only for messages it wants to swallow (e.g. mouse capture).
    if (g_imgui_ready && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
        return TRUE;

    // 3. When the menu is open, block remaining keyboard messages from reaching the game.
    if (g_menu_open &&
        (msg == WM_KEYDOWN || msg == WM_KEYUP ||
         msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP ||
         msg == WM_CHAR || msg == WM_DEADCHAR)) {
        return 0;
    }

    // 4. Forward everything else to the original WndProc.
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
        PostMessage(hwnd, WM_NULL, 0, 0);
    }
}



static HRESULT WINAPI hk_EndScene(IDirect3DDevice9 *d) {
    menu_increment_frame();
    if (!g_dev) { g_dev = d; LOG("Device acquired"); }

    if (!g_game_hwnd && g_dev) {
        D3DDEVICE_CREATION_PARAMETERS cp;
        if (SUCCEEDED(g_dev->GetCreationParameters(&cp))) {
            g_game_hwnd = cp.hFocusWindow;
            if (g_game_hwnd) {
                hook_window(g_game_hwnd);
            }
        }
    }

    if (menu_get_frame() == 20) {
        install_input_hooks();
    }

    menu_update_input();
    update_cheats();

    return orig_EndScene(d);
}

static HRESULT WINAPI hk_Present(IDirect3DDevice9 *d, CONST RECT *pSource, CONST RECT *pDest, HWND hDestOverride, CONST RGNDATA *pDirty) {
    if (!g_dev) g_dev = d;

    D3DVIEWPORT9 vp;
    if (SUCCEEDED(d->GetViewport(&vp))) {
        g_sw = vp.Width;
        g_sh = vp.Height;
    }

    if (menu_should_be_ready() && !g_imgui_ready) {
        if (g_game_hwnd) {
            menu_init_imgui(d, g_game_hwnd);
            g_imgui_ready = 1;
        }
    }
    if (menu_should_be_ready() && !g_entity_count) {
        static int scanned = 0;
        if (!scanned) { scanned = 1; scan_entities(); }
    }

    if (g_imgui_ready) {
        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        menu_render_overlay();
        menu_render_debug();
        menu_render();

        ImGui::Render();
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    }

    return orig_Present(d, pSource, pDest, hDestOverride, pDirty);
}

static HRESULT WINAPI hk_Reset(IDirect3DDevice9 *d, D3DPRESENT_PARAMETERS *pp) {
    if (g_imgui_ready) {
        ImGui_ImplDX9_InvalidateDeviceObjects();
    }

    g_sw = pp->BackBufferWidth;
    g_sh = pp->BackBufferHeight;
    LOG("Reset: %dx%d windowed=%d", g_sw, g_sh, pp->Windowed);

    HRESULT hr = orig_Reset(d, pp);
    if (SUCCEEDED(hr) && g_imgui_ready) {
        ImGui_ImplDX9_CreateDeviceObjects();
    }
    return hr;
}

void hook_device(IDirect3DDevice9 *dev) {
    if (!dev || orig_EndScene) return;
    void **vt = *(void***)dev;
    if (!vt) return;

    size_t sz = 512 * sizeof(void*);
    g_fake_vt = (void**)VirtualAlloc(NULL, sz, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!g_fake_vt) return;

    memcpy(g_fake_vt, vt, sz);
    orig_EndScene = (HRESULT (WINAPI *)(IDirect3DDevice9*))g_fake_vt[42];
    g_fake_vt[42] = (void*)hk_EndScene;

    orig_Present = (HRESULT (WINAPI *)(IDirect3DDevice9*, CONST RECT*, CONST RECT*, HWND, CONST RGNDATA*))g_fake_vt[17];
    g_fake_vt[17] = (void*)hk_Present;

    orig_Reset = (HRESULT (WINAPI *)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*))g_fake_vt[16];
    g_fake_vt[16] = (void*)hk_Reset;

    DWORD old;
    VirtualProtect(dev, sizeof(void*), PAGE_READWRITE, &old);
    *(void***)dev = g_fake_vt;
    VirtualProtect(dev, sizeof(void*), old, &old);

    LOG("Hooked! vtable copied: %d entries", 512);
}

static IDirect3D9* (WINAPI *real_D3DCreate9)(UINT) = NULL;
static HRESULT (WINAPI *real_CreateDevice)(IDirect3D9*, UINT, D3DDEVTYPE, HWND,
                                            DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**) = NULL;

static HRESULT WINAPI hk_CreateDevice(IDirect3D9 *d3d, UINT Adapter, D3DDEVTYPE Type,
                                       HWND hWnd, DWORD Flags, D3DPRESENT_PARAMETERS *pp,
                                       IDirect3DDevice9 **ppDev) {
    g_sw = pp->BackBufferWidth; g_sh = pp->BackBufferHeight;
    g_game_hwnd = hWnd;
    LOG("CreateDevice: %dx%d windowed=%d hwnd=%p", g_sw, g_sh, pp->Windowed, hWnd);
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

    void **vt = *(void***)d3d;
    real_CreateDevice = (HRESULT (WINAPI *)(IDirect3D9*, UINT, D3DDEVTYPE, HWND,
                                              DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**))vt[16];
    DWORD old;
    VirtualProtect(&vt[16], sizeof(void*), PAGE_READWRITE, &old);
    vt[16] = (void*)hk_CreateDevice;
    VirtualProtect(&vt[16], sizeof(void*), old, &old);

    return d3d;
}

extern "C" __declspec(dllexport) HRESULT WINAPI Direct3DCreate9Ex(UINT sdk, IDirect3D9Ex **ex) {
    typedef HRESULT (WINAPI *fn)(UINT, IDirect3D9Ex**);
    fn f = NULL;
    char path[MAX_PATH]; GetSystemDirectoryA(path, MAX_PATH); strcat(path, "\\d3d9.dll");
    HMODULE h = LoadLibraryA(path); if (h) f = (fn)GetProcAddress(h, "Direct3DCreate9Ex");
    if (!f) return E_NOTIMPL;
    HRESULT hr = f(sdk, ex);
    if (SUCCEEDED(hr) && ex && *ex) {
        LOG("Direct3DCreate9Ex -> %p", *ex);
        void **vt = *(void***)*ex;
        real_CreateDevice = (HRESULT (WINAPI *)(IDirect3D9*, UINT, D3DDEVTYPE, HWND,
                                                  DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**))vt[16];
        DWORD old;
        VirtualProtect(&vt[16], sizeof(void*), PAGE_READWRITE, &old);
        vt[16] = (void*)hk_CreateDevice;
        VirtualProtect(&vt[16], sizeof(void*), old, &old);
        LOG("Direct3DCreate9Ex: CreateDevice hooked");
    }
    return hr;
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

/* LevelEditor constructor hook */
typedef void* (__fastcall *LevelEditorCtor_t)(void* thisptr, void* edx);
static LevelEditorCtor_t real_LevelEditor_ctor = NULL;
static void* g_level_editor = NULL;

static void* __fastcall hk_LevelEditor_ctor(void* thisptr, void* edx) {
    void* ret = real_LevelEditor_ctor(thisptr, edx);
    g_level_editor = ret;
    LOG("LevelEditor::LevelEditor(this=%p) -> %p", thisptr, ret);
    install_v12_hook();
    return ret;
}

void install_level_editor_hook(void) {
    if (real_LevelEditor_ctor) return;
    DWORD base = (DWORD)GetModuleHandleA(NULL);
    LPVOID target = (LPVOID)(base + LEVEL_EDITOR_CTOR_OFFSET);
    MH_STATUS st = MH_CreateHook(target, (LPVOID)hk_LevelEditor_ctor, (void**)&real_LevelEditor_ctor);
    if (st == MH_OK) {
        MH_EnableHook(target);
        LOG("LevelEditor hook installed at %p", target);
    } else {
        LOG("LevelEditor hook FAILED at %p (err=%d)", target, (int)st);
    }
}

void* get_level_editor(void) {
    return g_level_editor;
}

/* vfunction12 debug hook via vtable swap
 * LevelEditor vtable (RVA 0xBC105C), entry 11 (index 11) = vfunction12
 * Called as: __thiscall(LevelEditor *this, int *msg_struct, int *msg_id_ptr, int unused)
 * ECX=this, [ESP+4]=msg_struct, [ESP+8]=msg_id_ptr, [ESP+12]=unused, RET 0xc
 * msg_struct layout: [header=0, unknown=0, cond, data_ptr]
 *   cond = 0 → no-op (function returns immediately)
 *   cond ≠ 0 → processes msg_id from *msg_id_ptr
 *
 * NOTE: All offsets here are RVAs (Ghidra_address - ImageBase 0x400000).
 *   Ghidra absolute addresses (0x00XXXXXX) need 0x400000 subtracted. */
#define DAT_ENTITY_TABLE  0xB56210

typedef void (__thiscall *V12Fn_t)(void* thisptr, int* msg_struct, int* msg_id_ptr, int unused);
static V12Fn_t real_v12 = NULL;
static int g_v12_log_count = 0;
static int g_v12_our_call = 0;

static void __thiscall hk_v12(void* thisptr, int* msg_struct,
                               int* msg_id_ptr, int unused)
{
    int ours = g_v12_our_call;
    if (g_v12_log_count < 100 || ours) {
        g_v12_log_count++;
        int msg_id = msg_id_ptr ? *msg_id_ptr : -1;
        int cond = msg_struct ? msg_struct[2] : -1;
        void* data = msg_struct ? (void*)msg_struct[3] : NULL;
        LOG("v12(this=%p, msg_id=0x%X, cond=%d, data=%p, u=%d)%s",
            thisptr, msg_id, cond, data, unused, ours ? " <<< OUR CALL" : "");

        DWORD base = (DWORD)GetModuleHandleA(NULL);
        void *et = *(void**)(base + DAT_ENTITY_TABLE);
        LOG("  DAT_entity_table=%p", et);
        if (et && msg_id == 0xb22) {
            int count = 0;
            for (int i = 0; i < 300; i++) {
                if (*(int*)((int)et + 8 + i * 4)) count++;
            }
            LOG("  entities_in_table=%d/300", count);
        }
    }
    real_v12(thisptr, msg_struct, msg_id_ptr, unused);
}

void install_v12_hook(void) {
    if (real_v12) return;
    void *le = g_level_editor;
    if (!le) { LOG("v12 hook: no LevelEditor yet"); return; }

    /* Read the LevelEditor's vtable pointer */
    void **orig_vt = *(void***)le;
    LOG("v12 hook: LE=%p orig_vt=%p", le, orig_vt);

    /* Read the current vfunction12 address from the vtable */
    real_v12 = (V12Fn_t)orig_vt[11];
    LOG("v12 hook: current vtable[11]=%p", real_v12);

    /* Allocate a new vtable (12 entries, copied from original) */
    void **new_vt = (void**)VirtualAlloc(NULL, 16 * sizeof(void*),
                                          MEM_COMMIT | MEM_RESERVE,
                                          PAGE_READWRITE);
    if (!new_vt) { LOG("v12 hook: VirtualAlloc failed"); return; }
    memcpy(new_vt, orig_vt, 12 * sizeof(void*));

    /* Overwrite entry 11 with our hook */
    new_vt[11] = (void*)hk_v12;

    /* Patch the LevelEditor's vtable pointer */
    DWORD old;
    VirtualProtect(le, sizeof(void*), PAGE_READWRITE, &old);
    *(void***)le = new_vt;
    VirtualProtect(le, sizeof(void*), old, &old);

    LOG("v12 vtable swap done: orig_vt=%p new_vt=%p real_v12=%p hk_v12=%p",
        orig_vt, new_vt, real_v12, hk_v12);
}

void le_send_message(int msg_id, void *data) {
    void *le = g_level_editor;
    if (!le) { LOG("le_send_message: no LevelEditor"); return; }
    void **vt = *(void***)le;
    if (!vt) { LOG("le_send_message: no vtable"); return; }

    int msg[4] = { 0, 0, 1, (int)data };

    g_v12_our_call = 1;
    typedef void (__thiscall *fn_t)(void* thisptr, int* msg_struct,
                                     int* msg_id_ptr, int unused);
    fn_t fn = (fn_t)vt[11];
    fn(le, msg, &msg_id, 0);
    g_v12_our_call = 0;
    LOG("le_send_message(id=0x%X, data=%p) OK", msg_id, data);
}

void hooks_cleanup(void) {
    if (g_imgui_ready) {
        menu_release_imgui();
        g_imgui_ready = 0;
    }

    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();

    if (g_game_hwnd && g_orig_wndproc) {
        SetWindowLongPtr(g_game_hwnd, GWLP_WNDPROC, (LONG_PTR)g_orig_wndproc);
        g_orig_wndproc = NULL;
    }
    if (g_fake_vt) {
        VirtualFree(g_fake_vt, 0, MEM_RELEASE);
        g_fake_vt = NULL;
    }
}
