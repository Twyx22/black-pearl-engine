#include "hooks.h"
#include "utils.h"
#include "cheats.h"
#include "menu.h"
#include "input.h"
#include "config.h"
#include <MinHook.h>

static HWND g_game_hwnd = NULL;
static WNDPROC g_orig_wndproc = NULL;
static HRESULT (WINAPI *orig_EndScene)(IDirect3DDevice9*) = NULL;
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

static LRESULT CALLBACK hk_wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    int vk = (int)wParam;
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
        if (vk >= 0 && vk < 256) g_key_states[vk] = 1;
    } else if (msg == WM_KEYUP || msg == WM_SYSKEYUP) {
        if (vk >= 0 && vk < 256) g_key_states[vk] = 0;
    }

    if (g_menu_open &&
        (msg == WM_KEYDOWN || msg == WM_KEYUP ||
         msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP ||
         msg == WM_CHAR || msg == WM_DEADCHAR)) {
        return 0;
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
        PostMessage(hwnd, WM_NULL, 0, 0);
    }
}

static BOOL (WINAPI *real_PeekMessageA)(LPMSG, HWND, UINT, UINT, UINT) = NULL;

static BOOL WINAPI hk_PeekMessageA(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin,
                                    UINT wMsgFilterMax, UINT wRemoveMsg) {
    BOOL ret = real_PeekMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
    if (!ret || !g_menu_open) return ret;

    if (lpMsg->message == WM_KEYDOWN || lpMsg->message == WM_KEYUP ||
        lpMsg->message == WM_SYSKEYDOWN || lpMsg->message == WM_SYSKEYUP ||
        lpMsg->message == WM_CHAR || lpMsg->message == WM_DEADCHAR) {
        if (wRemoveMsg & PM_REMOVE) {
            lpMsg->message = WM_NULL;
            lpMsg->wParam = 0;
            lpMsg->lParam = 0;
        } else {
            MSG dummy;
            real_PeekMessageA(&dummy, hWnd, lpMsg->message, lpMsg->message, PM_REMOVE);
        }
        return FALSE;
    }
    return ret;
}

void hook_peekmessage(void) {
    if (real_PeekMessageA) return;
    if (MH_CreateHookApi(L"user32.dll", "PeekMessageA", (LPVOID)hk_PeekMessageA, (void**)&real_PeekMessageA) != MH_OK) {
        LOG("PeekMessageA hook failed");
    }
}

static HRESULT WINAPI hk_EndScene(IDirect3DDevice9 *d) {
    menu_increment_frame();
    if (!g_dev) { g_dev = d; LOG("Device acquired"); }

    D3DVIEWPORT9 vp;
    if (SUCCEEDED(d->GetViewport(&vp))) {
        g_sw = vp.Width;
        g_sh = vp.Height;
    }

    if (menu_should_be_ready() && g_dev) {
        menu_init_fonts(d);
    }
    if (menu_should_be_ready() && !g_entity_count) {
        static int scanned = 0;
        if (!scanned) { scanned = 1; scan_entities(); }
    }

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

    if (menu_get_frame() > 10 && menu_get_frame() % 120 == 0) {
        install_input_hooks();
    }

    menu_update_input();
    update_cheats();
    menu_render_overlay(d);
    menu_render_debug(d);
    menu_render(d);

    return orig_EndScene(d);
}

static HRESULT WINAPI hk_Reset(IDirect3DDevice9 *d, D3DPRESENT_PARAMETERS *pp) {
    menu_release_fonts();

    g_sw = pp->BackBufferWidth;
    g_sh = pp->BackBufferHeight;
    LOG("Reset: %dx%d windowed=%d", g_sw, g_sh, pp->Windowed);

    return orig_Reset(d, pp);
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

/* LevelEditor constructor hook */
typedef void* (__fastcall *LevelEditorCtor_t)(void* thisptr, void* edx);
static LevelEditorCtor_t real_LevelEditor_ctor = NULL;
static void* g_level_editor = NULL;

static void* __fastcall hk_LevelEditor_ctor(void* thisptr, void* edx) {
    void* ret = real_LevelEditor_ctor(thisptr, edx);
    g_level_editor = ret;
    LOG("LevelEditor::LevelEditor(this=%p) -> %p", thisptr, ret);
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

void le_send_message(int msg_id, void *data) {
    void *le = g_level_editor;
    if (!le) { LOG("le_send_message: no LevelEditor"); return; }
    void **vt = *(void***)le;
    if (!vt) { LOG("le_send_message: no vtable"); return; }

    /* msg struct: { unused, unused, condition=0, data_ptr } */
    int msg[4] = { 0, 0, 0, (int)data };

    /*
     * vfunction12 is __thiscall with 3 stack params:
     *   void __thiscall(LevelEditor *this, int *msg_struct, int *msg_id_ptr, int unused)
     * - [EBP+0x8] = msg_struct[2] must be 0
     * - [ESP+0x10] = msg_id_ptr is DEREFERENCED (*msg_id_ptr = message ID)
     * - RET 0xc cleans 3 params (12 bytes)
     *
     * Use __fastcall wrapper: ECX=thisptr, EDX=garbage, then stack params.
     */
    typedef void (__fastcall *fn_t)(void* thisptr, void* edx,
                                     int* msg_struct, int* msg_id_ptr, int unused);
    fn_t fn = (fn_t)vt[11];
    fn(le, NULL, msg, &msg_id, 0);
    LOG("le_send_message(id=0x%X, data=%p)", msg_id, data);
}

void hooks_cleanup(void) {
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
