#include "hooks.h"
#include "utils.h"
#include "cheats.h"
#include "menu.h"
#include "uw.h"
#include "input.h"
#include "config.h"
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
    if (!g_speed_tic_init) {
        g_speed_tic_last_real = real;
        g_speed_tic_last_out = real;
        g_speed_tic_init = 1;
        return real;
    }
    DWORD delta = real - g_speed_tic_last_real;
    if (delta) {
        g_speed_tic_last_real = real;
        g_speed_tic_last_out += delta * g_cheats.speed_mult;
    }
    return g_speed_tic_last_out;
}

static LONGLONG scale_qpc_delta(LONGLONG real) {
    if (!g_speed_qpc_init) {
        g_speed_qpc_last_real = real;
        g_speed_qpc_last_out = real;
        g_speed_qpc_init = 1;
        return real;
    }
    LONGLONG delta = real - g_speed_qpc_last_real;
    if (delta) {
        g_speed_qpc_last_real = real;
        g_speed_qpc_last_out += delta * g_cheats.speed_mult;
    }
    return g_speed_qpc_last_out;
}

static void reset_speed_tic(void) {
    g_speed_tic_init = 0;
}

static void reset_speed_qpc(void) {
    g_speed_qpc_init = 0;
}

static DWORD WINAPI hk_GetTickCount(void) {
    DWORD real = real_GetTickCount();
    if (g_cheats.time_freeze) return g_freeze_tick;
    if (g_cheats.speed_mult > 1) return scale_tic_delta(real);
    reset_speed_tic();
    return real;
}

static BOOL WINAPI hk_QueryPerformanceCounter(LARGE_INTEGER *lpCount) {
    if (!lpCount) return real_QueryPerformanceCounter(lpCount);
    BOOL ok = real_QueryPerformanceCounter(lpCount);
    if (!ok) return FALSE;
    if (g_cheats.time_freeze) {
        lpCount->QuadPart = g_freeze_perf;
        return TRUE;
    }
    if (g_cheats.speed_mult > 1) {
        lpCount->QuadPart = scale_qpc_delta(lpCount->QuadPart);
        return TRUE;
    }
    reset_speed_qpc();
    return TRUE;
}

static DWORD WINAPI hk_timeGetTime(void) {
    DWORD real = real_timeGetTime();
    if (g_cheats.time_freeze) return g_freeze_tick;
    if (g_cheats.speed_mult > 1) return scale_tic_delta(real);
    reset_speed_tic();
    return real;
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
