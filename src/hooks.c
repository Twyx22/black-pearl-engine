#include "hooks.h"
#include "utils.h"
#include "cheats.h"
#include "menu.h"
#include "uw.h"
#include "input.h"
#include "config.h"
#include "water.h"
#include "presets.h"
#include "favorites.h"
#include "hotkeys.h"
#include "free_camera.h"
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx9.h"
#include <MinHook.h>
#include <d3dx9.h>
#include <d3dx9math.h>
#include <mmsystem.h>

HWND g_game_hwnd = NULL;
int g_imgui_ready = 0;
static WNDPROC g_orig_wndproc = NULL;
static HRESULT (WINAPI *orig_EndScene)(IDirect3DDevice9*) = NULL;
static HRESULT (WINAPI *orig_Present)(IDirect3DDevice9*, CONST RECT*, CONST RECT*, HWND, CONST RGNDATA*) = NULL;
static void **g_fake_vt = NULL;
static HRESULT (WINAPI *orig_Reset)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*) = NULL;

static int g_time_hooks_installed = 0;
int g_mouse_x = 0, g_mouse_y = 0;
int g_mouse_lb = 0, g_mouse_rb = 0;

DWORD (WINAPI *real_GetTickCount)(void) = NULL;
BOOL (WINAPI *real_QueryPerformanceCounter)(LARGE_INTEGER*) = NULL;
DWORD (WINAPI *real_timeGetTime)(void) = NULL;
DWORD g_freeze_tick = 0;
LONGLONG g_freeze_perf = 0;

/* Per-frame speed tracking for GetTickCount / timeGetTime (DWORD ms) */
static DWORD g_speed_tic_last_real = 0;
static DWORD g_speed_tic_last_out = 0;
static int g_speed_tic_init = 0;

/* Per-frame speed tracking for QPC (LONGLONG) */
static LONGLONG g_speed_qpc_last_real = 0;
static LONGLONG g_speed_qpc_last_out = 0;
static int g_speed_qpc_init = 0;

void time_freeze_snapshot(void) {
    g_freeze_tick = real_GetTickCount ? real_GetTickCount() : GetTickCount();
    LARGE_INTEGER li;
    if (real_QueryPerformanceCounter ? real_QueryPerformanceCounter(&li) : QueryPerformanceCounter(&li)) {
        g_freeze_perf = li.QuadPart;
    }
}

static DWORD scale_tic_delta(DWORD real) {
    int mult = g_cheats.speed_mult > 1 ? g_cheats.speed_mult : 1;
    if (!g_speed_tic_init) {
        g_speed_tic_last_real = real;
        g_speed_tic_last_out = real;
        g_speed_tic_init = 1;
        return real;
    }
    DWORD delta = real - g_speed_tic_last_real;
    if (delta) {
        g_speed_tic_last_real = real;
        g_speed_tic_last_out += delta * mult;
    }
    return g_speed_tic_last_out;
}

static LONGLONG scale_qpc_delta(LONGLONG real) {
    int mult = g_cheats.speed_mult > 1 ? g_cheats.speed_mult : 1;
    if (!g_speed_qpc_init) {
        g_speed_qpc_last_real = real;
        g_speed_qpc_last_out = real;
        g_speed_qpc_init = 1;
        return real;
    }
    LONGLONG delta = real - g_speed_qpc_last_real;
    if (delta) {
        g_speed_qpc_last_real = real;
        g_speed_qpc_last_out += delta * mult;
    }
    return g_speed_qpc_last_out;
}

/* System fallback resolvers (GetProcAddress bypasses our own MinHook
   detours, so no recursion even if real_* is NULL). Resolved once. */
static DWORD WINAPI sys_GetTickCount(void) {
    static DWORD (WINAPI *f)(void) = NULL;
    static int init = 0;
    if (!init) {
        init = 1;
        HMODULE k = GetModuleHandleA("kernel32.dll");
        if (k) f = (DWORD (WINAPI *)(void))GetProcAddress(k, "GetTickCount");
    }
    return f ? f() : 0;
}

static BOOL WINAPI sys_QueryPerformanceCounter(LARGE_INTEGER *lp) {
    static BOOL (WINAPI *f)(LARGE_INTEGER*) = NULL;
    static int init = 0;
    if (!init) {
        init = 1;
        HMODULE k = GetModuleHandleA("kernel32.dll");
        if (k) f = (BOOL (WINAPI *)(LARGE_INTEGER*))GetProcAddress(k, "QueryPerformanceCounter");
    }
    return f ? f(lp) : FALSE;
}

static DWORD WINAPI sys_timeGetTime(void) {
    static DWORD (WINAPI *f)(void) = NULL;
    static int init = 0;
    if (!init) {
        init = 1;
        HMODULE k = GetModuleHandleA("winmm.dll");
        if (k) f = (DWORD (WINAPI *)(void))GetProcAddress(k, "timeGetTime");
    }
    return f ? f() : 0;
}

static DWORD WINAPI hk_GetTickCount(void) {
    DWORD real = real_GetTickCount ? real_GetTickCount() : sys_GetTickCount();
    if (g_cheats.time_freeze) return g_freeze_tick;
    return scale_tic_delta(real);
}

static BOOL WINAPI hk_QueryPerformanceCounter(LARGE_INTEGER *lpCount) {
    if (!lpCount) return FALSE;
    BOOL ok;
    if (real_QueryPerformanceCounter) {
        ok = real_QueryPerformanceCounter(lpCount);
    } else {
        ok = sys_QueryPerformanceCounter(lpCount);
    }
    if (!ok) return FALSE;
    if (g_cheats.time_freeze) {
        lpCount->QuadPart = g_freeze_perf;
        return TRUE;
    }
    lpCount->QuadPart = scale_qpc_delta(lpCount->QuadPart);
    return TRUE;
}

static DWORD WINAPI hk_timeGetTime(void) {
    DWORD real = real_timeGetTime ? real_timeGetTime() : sys_timeGetTime();
    if (g_cheats.time_freeze) return g_freeze_tick;
    return scale_tic_delta(real);
}

void install_time_hooks(void) {
    if (g_time_hooks_installed) return;
    if (MH_CreateHookApi(L"kernel32.dll", "GetTickCount", (LPVOID)hk_GetTickCount, (void**)&real_GetTickCount) != MH_OK) {
        LOG("GetTickCount hook failed");
    }
    if (MH_CreateHookApi(L"kernel32.dll", "QueryPerformanceCounter", (LPVOID)hk_QueryPerformanceCounter, (void**)&real_QueryPerformanceCounter) != MH_OK) {
        LOG("QueryPerformanceCounter hook failed");
    }
    if (MH_CreateHookApi(L"winmm.dll", "timeGetTime", (LPVOID)hk_timeGetTime, (void**)&real_timeGetTime) != MH_OK) {
        LOG("timeGetTime hook failed");
    }
    g_time_hooks_installed = 1;
    LOG("Time hooks installed via MinHook");
    hotkeys_init();
    presets_init();
    favorites_init();
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static LRESULT CALLBACK hk_wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
        int vk = (int)wParam;
        if (vk >= 0 && vk < 256) g_key_states[vk] = 1;
    } else if (msg == WM_KEYUP || msg == WM_SYSKEYUP) {
        int vk = (int)wParam;
        if (vk >= 0 && vk < 256) g_key_states[vk] = 0;
    } else if (msg == WM_MOUSEMOVE) {
        g_mouse_x = LOWORD(lParam);
        g_mouse_y = HIWORD(lParam);
    } else if (msg == WM_LBUTTONDOWN) {
        g_mouse_lb = 1;
    } else if (msg == WM_LBUTTONUP) {
        g_mouse_lb = 0;
    } else if (msg == WM_RBUTTONDOWN) {
        g_mouse_rb = 1;
    } else if (msg == WM_RBUTTONUP) {
        g_mouse_rb = 0;
    }

    if (g_menu_open) {
        if (g_imgui_ready && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
            return TRUE;
        if (msg == WM_KEYDOWN || msg == WM_KEYUP ||
            msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP ||
            msg == WM_CHAR || msg == WM_DEADCHAR ||
            (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) ||
            msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL) {
            return 0;
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
        PostMessage(hwnd, WM_NULL, 0, 0);
    }
}

void hooks_cleanup(void) {
    if (g_game_hwnd && g_orig_wndproc) {
        SetWindowLongPtr(g_game_hwnd, GWLP_WNDPROC, (LONG_PTR)g_orig_wndproc);
        LOG("Window subclass restored: hwnd=%p", g_game_hwnd);
        g_orig_wndproc = NULL;
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

    /* Free camera input (handled in menu_update_input if active) */
    update_cheats();

    return orig_EndScene(d);
}

static HRESULT WINAPI hk_Present(IDirect3DDevice9 *d, CONST RECT *pSource, CONST RECT *pDest, HWND hDestOverride, CONST RGNDATA *pDirty) {
    if (!g_dev) g_dev = d;

    HRESULT coop = d->TestCooperativeLevel();
    int device_ok = (coop == D3D_OK);

    D3DVIEWPORT9 vp;
    if (device_ok && SUCCEEDED(d->GetViewport(&vp))) {
        g_sw = vp.Width;
        g_sh = vp.Height;
    }

    if (device_ok && menu_should_be_ready() && !g_imgui_ready) {
        if (g_game_hwnd) {
            menu_init_imgui(d, g_game_hwnd);
            g_imgui_ready = 1;
        }
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

    uw_set_orig_set_transform((HRESULT (WINAPI *)(IDirect3DDevice9*, D3DTRANSFORMSTATETYPE, const D3DMATRIX*))g_fake_vt[40]);
    g_fake_vt[40] = (void*)hk_SetTransform;

    DWORD old;
    VirtualProtect(dev, sizeof(void*), PAGE_READWRITE, &old);
    *(void***)dev = g_fake_vt;
    VirtualProtect(dev, sizeof(void*), old, &old);

    uw_init();
    water_init(dev);
    LOG("Hooked! vtable copied: %d entries (SetTransform hooked)", 512);
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

/* Missing d3d9 exports: forwarded to the real system d3d9 via
   GetSystemDirectoryA path (never "d3d9" by name: that would recurse
   into this proxy DLL). Same LoadLibraryA pattern as Direct3DCreate9. */
extern "C" __declspec(dllexport) HRESULT WINAPI Direct3DShaderValidatorCreate9(void) {
    typedef HRESULT (WINAPI *fn)(void);
    fn f = NULL;
    char path[MAX_PATH]; GetSystemDirectoryA(path, MAX_PATH); strcat(path, "\\d3d9.dll");
    HMODULE h = LoadLibraryA(path); if (h) f = (fn)GetProcAddress(h, "Direct3DShaderValidatorCreate9");
    return f ? f() : E_NOTIMPL;
}

extern "C" __declspec(dllexport) DWORD WINAPI D3DPERF_GetStatus(void) {
    typedef DWORD (WINAPI *fn)(void);
    fn f = NULL;
    char path[MAX_PATH]; GetSystemDirectoryA(path, MAX_PATH); strcat(path, "\\d3d9.dll");
    HMODULE h = LoadLibraryA(path); if (h) f = (fn)GetProcAddress(h, "D3DPERF_GetStatus");
    return f ? f() : 0;
}

extern "C" __declspec(dllexport) BOOL WINAPI D3DPERF_QueryRepeatFrame(void) {
    typedef BOOL (WINAPI *fn)(void);
    fn f = NULL;
    char path[MAX_PATH]; GetSystemDirectoryA(path, MAX_PATH); strcat(path, "\\d3d9.dll");
    HMODULE h = LoadLibraryA(path); if (h) f = (fn)GetProcAddress(h, "D3DPERF_QueryRepeatFrame");
    return f ? f() : FALSE;
}

extern "C" __declspec(dllexport) void WINAPI D3DPERF_SetMarker(D3DCOLOR color, LPCWSTR name) {
    typedef void (WINAPI *fn)(D3DCOLOR, LPCWSTR);
    fn f = NULL;
    char path[MAX_PATH]; GetSystemDirectoryA(path, MAX_PATH); strcat(path, "\\d3d9.dll");
    HMODULE h = LoadLibraryA(path); if (h) f = (fn)GetProcAddress(h, "D3DPERF_SetMarker");
    if (f) f(color, name);
}

extern "C" __declspec(dllexport) void WINAPI D3DPERF_SetOptions(DWORD options) {
    typedef void (WINAPI *fn)(DWORD);
    fn f = NULL;
    char path[MAX_PATH]; GetSystemDirectoryA(path, MAX_PATH); strcat(path, "\\d3d9.dll");
    HMODULE h = LoadLibraryA(path); if (h) f = (fn)GetProcAddress(h, "D3DPERF_SetOptions");
    if (f) f(options);
}

extern "C" __declspec(dllexport) void WINAPI D3DPERF_SetRegion(D3DCOLOR color, LPCWSTR name) {
    typedef void (WINAPI *fn)(D3DCOLOR, LPCWSTR);
    fn f = NULL;
    char path[MAX_PATH]; GetSystemDirectoryA(path, MAX_PATH); strcat(path, "\\d3d9.dll");
    HMODULE h = LoadLibraryA(path); if (h) f = (fn)GetProcAddress(h, "D3DPERF_SetRegion");
    if (f) f(color, name);
}

extern "C" __declspec(dllexport) void WINAPI DebugSetLevel(DWORD level) {
    typedef void (WINAPI *fn)(DWORD);
    fn f = NULL;
    char path[MAX_PATH]; GetSystemDirectoryA(path, MAX_PATH); strcat(path, "\\d3d9.dll");
    HMODULE h = LoadLibraryA(path); if (h) f = (fn)GetProcAddress(h, "DebugSetLevel");
    if (f) f(level);
}

extern "C" __declspec(dllexport) void WINAPI DebugSetMute(BOOL mute) {
    typedef void (WINAPI *fn)(BOOL);
    fn f = NULL;
    char path[MAX_PATH]; GetSystemDirectoryA(path, MAX_PATH); strcat(path, "\\d3d9.dll");
    HMODULE h = LoadLibraryA(path); if (h) f = (fn)GetProcAddress(h, "DebugSetMute");
    if (f) f(mute);
}
