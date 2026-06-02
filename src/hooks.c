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
#include <d3dx9.h>
#include <d3dx9math.h>

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

D3DVIEWPORT9 g_viewport;
D3DMATRIX g_view_mat, g_proj_mat;
int g_camera_valid = 0;

/* Editor free-fly camera */
D3DMATRIX g_editor_view;
float g_editor_cam_pos[3] = {0,0,0};
float g_editor_cam_yaw = 0, g_editor_cam_pitch = 0;
int g_editor_cam_initialized = 0;

static HRESULT (WINAPI *orig_SetTransform)(IDirect3DDevice9*, D3DTRANSFORMSTATETYPE, const D3DMATRIX*) = NULL;
static HRESULT (WINAPI *orig_SetVertexShaderConstantF)(IDirect3DDevice9*, UINT, const float*, UINT) = NULL;

int g_settransform_log_count = 0;
int g_shader_const_log_count = 0;

/* Projection matrix extracted from first camera upload */
D3DXMATRIX g_proj_from_game;
int g_proj_captured = 0;

/* Saved regs 9-12 (non-view data passed through unchanged) */
float g_saved_reg9[4], g_saved_reg10[4], g_saved_reg11[4], g_saved_reg12[4];
int g_extra_saved = 0;

static HRESULT WINAPI hk_SetTransform(IDirect3DDevice9 *d, D3DTRANSFORMSTATETYPE State, const D3DMATRIX *pMatrix) {
    if (g_editor_enabled && g_editor_cam_initialized && State == D3DTS_VIEW) {
        return orig_SetTransform(d, State, &g_editor_view);
    }
    return orig_SetTransform(d, State, pMatrix);
}

/* Build Camera World matrix (inverse of view) from g_editor_view + cam_pos.
 * g_editor_view (from D3DXMatrixLookAtLH):
 *   row0: [right.x, right.y, right.z, 0]
 *   row1: [up.x,    up.y,    up.z,   0]
 *   row2: [look.x,  look.y,  look.z, 0]
 *   row3: [-dot(r,eye), -dot(u,eye), -dot(l,eye), 1]
 *
 * Camera World stored column-major as HLSL float4x4:
 *   reg4: [right.x, right.y, right.z, 0]    = [view._11, view._21, view._31, 0]
 *   reg5: [up.x, up.y, up.z, 0]             = [view._12, view._22, view._32, 0]
 *   reg6: [look.x, look.y, look.z, 0]       = [view._13, view._23, view._33, 0]
 *   reg7: [eye.x, eye.y, eye.z, 1]
 */
static void build_camera_world(D3DMATRIX *out) {
    out->_11 = g_editor_view._11; out->_12 = g_editor_view._21; out->_13 = g_editor_view._31; out->_14 = 0.0f;
    out->_21 = g_editor_view._12; out->_22 = g_editor_view._22; out->_23 = g_editor_view._32; out->_24 = 0.0f;
    out->_31 = g_editor_view._13; out->_32 = g_editor_view._23; out->_33 = g_editor_view._33; out->_34 = 0.0f;
    out->_41 = g_editor_cam_pos[0]; out->_42 = g_editor_cam_pos[1]; out->_43 = g_editor_cam_pos[2]; out->_44 = 1.0f;
}

static HRESULT WINAPI hk_SetVertexShaderConstantF(IDirect3DDevice9 *d, UINT StartRegister, const float *pConstantData, UINT Vector4fCount) {
    if (g_editor_enabled && g_editor_cam_initialized) {
        if (g_shader_const_log_count < 1) {
            g_shader_const_log_count++;
            char buf[1024] = {0};
            int pos = 0;
            UINT count = (Vector4fCount < 16) ? Vector4fCount : 16;
            for (UINT i = 0; i < count && pos < 1000; i++) {
                pos += snprintf(buf + pos, sizeof(buf) - pos,
                    "[%d]: %.4f %.4f %.4f %.4f\n", StartRegister + i,
                    pConstantData[i*4], pConstantData[i*4+1],
                    pConstantData[i*4+2], pConstantData[i*4+3]);
            }
            LOG("SetVertexShaderConstantF(reg=%d count=%d):\n%s", StartRegister, Vector4fCount, buf);
        }

        /* Camera data upload: reg=0 count=13+ (sometimes other counts).
         * Layout deduced from analysis:
         *   reg 0-3: ViewProj (View * Proj)
         *   reg 4-7: Camera World (inverse of View) — confirmed via pos extraction
         *   reg 8:   Camera world position [xyz, 1]
         *   reg 9:   Near/far plane data
         *   reg 10+: Other camera params
         *
         * We replace view-dependent regs while extracting Proj on first frame
         * and passing through regs 9-12 unchanged. */
        if (StartRegister == 0 && Vector4fCount >= 9 && g_editor_cam_initialized) {
            float modified[64];
            memcpy(modified, pConstantData, Vector4fCount * 4 * sizeof(float));

            /* First capture: save regs 9-12, extract Proj = CamWorld * ViewProj */
            if (!g_proj_captured) {
                if (Vector4fCount > 9)
                    memcpy(g_saved_reg9, pConstantData + 36, sizeof(float) * 4);
                if (Vector4fCount > 10)
                    memcpy(g_saved_reg10, pConstantData + 40, sizeof(float) * 4);
                if (Vector4fCount > 11)
                    memcpy(g_saved_reg11, pConstantData + 44, sizeof(float) * 4);
                if (Vector4fCount > 12)
                    memcpy(g_saved_reg12, pConstantData + 48, sizeof(float) * 4);
                g_extra_saved = 1;

                D3DXMATRIX orig_camworld, orig_viewproj;
                memcpy(&orig_camworld, pConstantData + 16, sizeof(D3DMATRIX));
                memcpy(&orig_viewproj, pConstantData, sizeof(D3DMATRIX));
                D3DXMatrixMultiply(&g_proj_from_game, (const D3DXMATRIX*)&orig_camworld, (const D3DXMATRIX*)&orig_viewproj);
                g_proj_captured = 1;
                LOG("Projection captured from game camera data");
                /* Search memory for the game's camera position to patch it for culling */
                if (Vector4fCount > 8) {
                    editor_scan_camera_pos(pConstantData + 32);
                }
            }

            /* Our ViewProj = our_View * Proj */
            D3DXMATRIX our_viewproj;
            D3DXMatrixMultiply(&our_viewproj, (const D3DXMATRIX*)&g_editor_view, (const D3DXMATRIX*)&g_proj_from_game);
            memcpy(modified, &our_viewproj, sizeof(D3DMATRIX));

            /* Our CamWorld = inverse(our_View) */
            D3DMATRIX our_camworld;
            build_camera_world(&our_camworld);
            memcpy(modified + 16, &our_camworld, sizeof(D3DMATRIX));

            /* Camera position (reg 8) */
            modified[32] = g_editor_cam_pos[0];
            modified[33] = g_editor_cam_pos[1];
            modified[34] = g_editor_cam_pos[2];
            modified[35] = 1.0f;

            /* Pass through regs 9-12 */
            if (g_extra_saved && Vector4fCount > 9)
                memcpy(modified + 36, g_saved_reg9, sizeof(float) * 4);
            if (g_extra_saved && Vector4fCount > 10)
                memcpy(modified + 40, g_saved_reg10, sizeof(float) * 4);
            if (g_extra_saved && Vector4fCount > 11)
                memcpy(modified + 44, g_saved_reg11, sizeof(float) * 4);
            if (g_extra_saved && Vector4fCount > 12)
                memcpy(modified + 48, g_saved_reg12, sizeof(float) * 4);

            return orig_SetVertexShaderConstantF(d, StartRegister, modified, Vector4fCount);
        }
    }
    return orig_SetVertexShaderConstantF(d, StartRegister, pConstantData, Vector4fCount);
}

/* Memory scan for game's camera position.
 * After we detect the camera position from reg 8 of the shader constants,
 * we search writable memory for those 3 floats and save the address.
 * Each frame we overwrite that address with our editor camera position. */
static float *g_cam_pos_mem = NULL;

void editor_scan_camera_pos(const float *cam_pos) {
    g_cam_pos_mem = NULL;
    float x = cam_pos[0], y = cam_pos[1], z = cam_pos[2];

    MEMORY_BASIC_INFORMATION mbi;
    unsigned char *addr = (unsigned char*)0x00400000;
    while (addr < (unsigned char*)0x7FFE0000) {
        if (VirtualQuery(addr, &mbi, sizeof(mbi)) == 0) break;
        if (mbi.State == MEM_COMMIT && (mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE))) {
            float *scan = (float*)mbi.BaseAddress;
            int len = mbi.RegionSize / sizeof(float);
            for (int i = 0; i < len - 2; i++) {
                if (fabsf(scan[i] - x) < 0.01f && fabsf(scan[i+1] - y) < 0.01f && fabsf(scan[i+2] - z) < 0.01f) {
                    g_cam_pos_mem = &scan[i];
                    LOG("Camera pos found at %p", g_cam_pos_mem);
                    return;
                }
            }
        }
        addr = (unsigned char*)mbi.BaseAddress + mbi.RegionSize;
    }
    LOG("Camera position NOT found in memory");
}

static int g_selected_entities[300] = {0};
static int g_selected_count = 0;

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
    // 1. Track keyboard state for our own menu navigation (always, menu open or not)
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

    // 2. When menu is open: route ALL input through ImGui, block from game
    if (g_menu_open) {
        // Pass to ImGui first
        if (g_imgui_ready && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
            return TRUE;

        // Block ALL input messages from reaching the game
        if (msg == WM_KEYDOWN || msg == WM_KEYUP ||
            msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP ||
            msg == WM_CHAR || msg == WM_DEADCHAR ||
            (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) ||
            msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL) {
            return 0;
        }
    }

    // 3. Menu closed or non-input message: forward to original WndProc
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
    if (SUCCEEDED(d->GetViewport(&g_viewport)) &&
        SUCCEEDED(d->GetTransform(D3DTS_VIEW, &g_view_mat)) &&
        SUCCEEDED(d->GetTransform(D3DTS_PROJECTION, &g_proj_mat))) {
        g_camera_valid = 1;
    }

    if (menu_should_be_ready() && !g_imgui_ready) {
        if (g_game_hwnd) {
            menu_init_imgui(d, g_game_hwnd);
            g_imgui_ready = 1;
        }
    }
    if (menu_should_be_ready()) {
        static int scanned = 0;
        static int scanned_editor = 0;
        if (!scanned && !g_entity_count) { scanned = 1; scan_entities(); }
        if (!scanned_editor) { scanned_editor = 1; scan_entity_table(); }
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

    /* Editor camera: init once, update every frame */
    if (g_editor_enabled && g_camera_valid && !g_editor_cam_initialized) {
        LOG("Present: initializing editor camera");
        editor_cam_init();
    }
    if (g_editor_enabled && g_editor_cam_initialized) {
        editor_cam_update();
    }

    /* Editor mouse picking */
    if (g_editor_enabled && !g_menu_open && g_camera_valid) {
        static int prev_lb = 0;
        if (g_mouse_lb && !prev_lb) {
            int picked = pick_entity_at_screen((float)g_mouse_x, (float)g_mouse_y);
            if (picked >= 0) {
                int found = 0;
                for (int i = 0; i < g_selected_count; i++) {
                    if (g_selected_entities[i] == picked) { found = 1; break; }
                }
                if (!found) editor_select_entity(picked);
                LOG("Editor pick: entity[%d]", picked);
            }
        }
        prev_lb = g_mouse_lb;
    }

    /* Editor keyboard movement: I=forward J=left K=back L=right U=up O=down */
    if (g_editor_enabled && !g_menu_open && g_selected_count > 0) {
        float speed = (g_key_states[VK_SHIFT] || g_key_states[VK_LSHIFT] || g_key_states[VK_RSHIFT]) ? 2.0f : 0.2f;
        float dx = 0, dy = 0, dz = 0;
        D3DXVECTOR3 fwd(g_view_mat._13, g_view_mat._23, g_view_mat._33);
        D3DXVECTOR3 right(g_view_mat._11, g_view_mat._21, g_view_mat._31);
        D3DXVECTOR3 up(g_view_mat._12, g_view_mat._22, g_view_mat._32);
        if (g_key_states['I']) { dx += fwd.x * speed; dy += fwd.y * speed; dz += fwd.z * speed; }
        if (g_key_states['K']) { dx -= fwd.x * speed; dy -= fwd.y * speed; dz -= fwd.z * speed; }
        if (g_key_states['J']) { dx -= right.x * speed; dy -= right.y * speed; dz -= right.z * speed; }
        if (g_key_states['L']) { dx += right.x * speed; dy += right.y * speed; dz += right.z * speed; }
        if (g_key_states['U']) { dx += up.x * speed; dy += up.y * speed; dz += up.z * speed; }
        if (g_key_states['O']) { dx -= up.x * speed; dy -= up.y * speed; dz -= up.z * speed; }
        if (dx != 0 || dy != 0 || dz != 0) {
            editor_move_selected(dx, dy, dz);
        }
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

    orig_SetTransform = (HRESULT (WINAPI *)(IDirect3DDevice9*, D3DTRANSFORMSTATETYPE, const D3DMATRIX*))g_fake_vt[44];
    g_fake_vt[44] = (void*)hk_SetTransform;

    orig_SetVertexShaderConstantF = (HRESULT (WINAPI *)(IDirect3DDevice9*, UINT, const float*, UINT))g_fake_vt[94];
    g_fake_vt[94] = (void*)hk_SetVertexShaderConstantF;
    LOG("hook_device: SetTransform[44]=%p SetVertexShaderConstantF[94]=%p", orig_SetTransform, orig_SetVertexShaderConstantF);

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
 *   cond = 0 → PROCESSES msg_id (game uses this)
 *   cond ≠ 0 → no-op (returns immediately, IGNORED!)
 *
 * NOTE: All offsets here are RVAs (Ghidra_address - ImageBase 0x400000).
 *   Ghidra absolute addresses (0x00XXXXXX) need 0x400000 subtracted. */
#define DAT_ENTITY_TABLE  0xB56210

typedef void (__thiscall *V12Fn_t)(void* thisptr, int* msg_struct, int* msg_id_ptr, int unused);
static V12Fn_t real_v12 = NULL;
int g_editor_entity_count = 0;
static int g_v12_log_count = 0;
int g_v12_our_call = 0;
int g_editor_enabled = 0;
int g_editor_auto_cycle = 0;

static void __thiscall hk_v12(void* thisptr, int* msg_struct,
                               int* msg_id_ptr, int unused)
{
    int ours = g_v12_our_call;
    if (ours) {
        int msg_id = msg_id_ptr ? *msg_id_ptr : -1;
        LOG("hk_v12: OUR msg_id=0x%X", msg_id);
        int cond = msg_struct ? msg_struct[2] : -1;
        void* data = msg_struct ? (void*)msg_struct[3] : NULL;
        LOG("v12(ours, msg_id=0x%X, cond=%d, data=%p)", msg_id, cond, data);

        /* 0xE10 camera toggle is broken (DAT_00f66bc4 is NULL during gameplay).
         * Editor camera is now handled by SetTransform hook + custom free-fly. */
        if (msg_id == 0xE10) {
            DWORD base = (DWORD)GetModuleHandleA(NULL);
            void *cam = *(void**)(base + 0xB66BC4); /* DAT_00f66bc4 */
            LOG("  DAT_00f66bc4 (cam) = %p (bypassed: using custom camera)", cam);
        }
        if (msg_id == 0xb22) {
            DWORD base = (DWORD)GetModuleHandleA(NULL);
            void *et = *(void**)(base + DAT_ENTITY_TABLE);
            LOG("  DAT_entity_table=%p", et);
            if (et) {
                int total = 0, selectable = 0;
                for (int i = 0; i < 300; i++) {
                    int *ent = *(int**)((int)et + 8 + i * 4);
                    if (!ent) continue;
                    total++;
                    if (*(unsigned char*)((int)ent + 0x70) & 8) selectable++;
                }
                LOG("  entities: %d total, %d selectable (bit3@0x70)", total, selectable);
            }
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

void scan_entity_table(void) {
    DWORD base = (DWORD)GetModuleHandleA(NULL);
    void *et = *(void**)(base + DAT_ENTITY_TABLE);
    if (!et) return;
    int count = 0;
    for (int i = 0; i < 300; i++) {
        if (*(int*)((int)et + 8 + i * 4)) count++;
    }
    g_editor_entity_count = count;
    LOG("scan_entity_table: %d/300 entities, g_entity_table=%p", count, *(void**)(base + DAT_ENTITY_TABLE));
}

void editor_cam_init(void) {
    /* D3DMATRIX and D3DXMATRIX have identical layout */
    D3DMATRIX *m = &g_view_mat;
    g_editor_cam_pos[0] = -(m->_11*m->_41 + m->_21*m->_42 + m->_31*m->_43);
    g_editor_cam_pos[1] = -(m->_12*m->_41 + m->_22*m->_42 + m->_32*m->_43);
    g_editor_cam_pos[2] = -(m->_13*m->_41 + m->_23*m->_42 + m->_33*m->_43);

    float look_x = m->_31;
    float look_y = m->_32;
    float look_z = m->_33;
    g_editor_cam_yaw = atan2f(look_x, look_z);
    g_editor_cam_pitch = -asinf(look_y);

    g_editor_cam_initialized = 1;
    LOG("editor_cam_init: pos=(%.2f,%.2f,%.2f) yaw=%.2f pitch=%.2f",
        g_editor_cam_pos[0], g_editor_cam_pos[1], g_editor_cam_pos[2],
        g_editor_cam_yaw, g_editor_cam_pitch);
}

void editor_cam_update(void) {
    if (!g_editor_enabled || !g_camera_valid) return;
    if (!g_editor_cam_initialized) return;
    
    static int log_count = 0;
    if (log_count < 3) {
        log_count++;
        LOG("editor_cam_update: pos=(%.2f,%.2f,%.2f) yaw=%.2f pitch=%.2f",
            g_editor_cam_pos[0], g_editor_cam_pos[1], g_editor_cam_pos[2],
            g_editor_cam_yaw, g_editor_cam_pitch);
    }

    /* Keyboard look with arrow keys */
    if (g_key_states[VK_UP] || g_key_states['I']) {
        g_editor_cam_pitch += 1.5f / 60.0f;
    }
    if (g_key_states[VK_DOWN] || g_key_states['K']) {
        g_editor_cam_pitch -= 1.5f / 60.0f;
    }
    if (g_key_states[VK_LEFT] || g_key_states['J']) {
        g_editor_cam_yaw += 2.0f / 60.0f;
    }
    if (g_key_states[VK_RIGHT] || g_key_states['L']) {
        g_editor_cam_yaw -= 2.0f / 60.0f;
    }
    if (g_editor_cam_pitch > 1.5f) g_editor_cam_pitch = 1.5f;
    if (g_editor_cam_pitch < -1.5f) g_editor_cam_pitch = -1.5f;

    float speed = 8.0f;
    if (g_key_states[VK_SHIFT]) speed = 25.0f;

    float fwd_x = cosf(g_editor_cam_yaw) * cosf(g_editor_cam_pitch);
    float fwd_y = sinf(g_editor_cam_pitch);
    float fwd_z = sinf(g_editor_cam_yaw) * cosf(g_editor_cam_pitch);
    float right_x = sinf(g_editor_cam_yaw);
    float right_z = -cosf(g_editor_cam_yaw);

    if (g_key_states['W']) {
        g_editor_cam_pos[0] += fwd_x * speed / 60.0f;
        g_editor_cam_pos[1] += fwd_y * speed / 60.0f;
        g_editor_cam_pos[2] += fwd_z * speed / 60.0f;
    }
    if (g_key_states['S']) {
        g_editor_cam_pos[0] -= fwd_x * speed / 60.0f;
        g_editor_cam_pos[1] -= fwd_y * speed / 60.0f;
        g_editor_cam_pos[2] -= fwd_z * speed / 60.0f;
    }
    if (g_key_states['A']) {
        g_editor_cam_pos[0] += right_x * speed / 60.0f;
        g_editor_cam_pos[2] += right_z * speed / 60.0f;
    }
    if (g_key_states['D']) {
        g_editor_cam_pos[0] -= right_x * speed / 60.0f;
        g_editor_cam_pos[2] -= right_z * speed / 60.0f;
    }
    if (g_key_states['E']) {
        g_editor_cam_pos[1] += speed / 60.0f;
    }
    if (g_key_states['Q']) {
        g_editor_cam_pos[1] -= speed / 60.0f;
    }

    D3DXVECTOR3 eye(g_editor_cam_pos[0], g_editor_cam_pos[1], g_editor_cam_pos[2]);
    D3DXVECTOR3 at(eye.x + fwd_x, eye.y + fwd_y, eye.z + fwd_z);
    D3DXVECTOR3 up(0, 1, 0);
    D3DXMatrixLookAtLH((D3DXMATRIX*)&g_editor_view, &eye, &at, &up);

    /* Also update g_view_mat so overlay 3D→2D math stays consistent */
    memcpy(&g_view_mat, &g_editor_view, sizeof(D3DMATRIX));

    /* Patch game's camera position in memory so frustum culling uses our pos */
    if (g_cam_pos_mem) {
        g_cam_pos_mem[0] = g_editor_cam_pos[0];
        g_cam_pos_mem[1] = g_editor_cam_pos[1];
        g_cam_pos_mem[2] = g_editor_cam_pos[2];
    }
}

void editor_clear_selection(void) {
    g_selected_count = 0;
    memset(g_selected_entities, 0, sizeof(g_selected_entities));
}

void editor_select_entity(int index) {
    if (index < 0 || index >= 300) return;
    if (g_selected_count < 300) {
        g_selected_entities[g_selected_count++] = index;
    }
}

typedef struct { float x, y, z, rhw; D3DCOLOR color; } EVertex;

int screen_to_world_ray(float screen_x, float screen_y,
                         float *out_origin, float *out_dir) {
    if (!g_camera_valid) return 0;
    D3DXMATRIX view_proj;
    D3DXMatrixMultiply(&view_proj, (const D3DXMATRIX*)&g_view_mat, (const D3DXMATRIX*)&g_proj_mat);
    D3DXMATRIX inv;
    if (D3DXMatrixInverse(&inv, NULL, &view_proj) == NULL) return 0;
    float ndc_x = 2.0f * screen_x / (float)g_viewport.Width - 1.0f;
    float ndc_y = 1.0f - 2.0f * screen_y / (float)g_viewport.Height;
    D3DXVECTOR4 near_pt((float)ndc_x, (float)ndc_y, 0.0f, 1.0f);
    D3DXVECTOR4 far_pt((float)ndc_x, (float)ndc_y, 1.0f, 1.0f);
    D3DXVECTOR4 near_w, far_w;
    D3DXVec4Transform(&near_w, &near_pt, &inv);
    D3DXVec4Transform(&far_w, &far_pt, &inv);
    if (near_w.w == 0.0f || far_w.w == 0.0f) return 0;
    out_origin[0] = near_w.x / near_w.w;
    out_origin[1] = near_w.y / near_w.w;
    out_origin[2] = near_w.z / near_w.w;
    out_dir[0] = far_w.x / far_w.w - out_origin[0];
    out_dir[1] = far_w.y / far_w.w - out_origin[1];
    out_dir[2] = far_w.z / far_w.w - out_origin[2];
    float len = sqrtf(out_dir[0]*out_dir[0] + out_dir[1]*out_dir[1] + out_dir[2]*out_dir[2]);
    if (len == 0) return 0;
    out_dir[0] /= len; out_dir[1] /= len; out_dir[2] /= len;
    return 1;
}

int pick_entity_at_screen(float screen_x, float screen_y) {
    float origin[3], dir[3];
    if (!screen_to_world_ray(screen_x, screen_y, origin, dir)) return -1;
    DWORD base = (DWORD)GetModuleHandleA(NULL);
    void *et = *(void**)(base + DAT_ENTITY_TABLE);
    if (!et) return -1;
    int best = -1;
    float best_d = 1e30f;
    for (int i = 0; i < 300; i++) {
        float pos[3];
        if (!get_entity_world_pos(i, pos)) continue;
        float dx = pos[0] - origin[0];
        float dy = pos[1] - origin[1];
        float dz = pos[2] - origin[2];
        float t = dx*dir[0] + dy*dir[1] + dz*dir[2];
        if (t < 0) continue;
        float closest_x = origin[0] + dir[0]*t;
        float closest_y = origin[1] + dir[1]*t;
        float closest_z = origin[2] + dir[2]*t;
        float dist = sqrtf((closest_x-pos[0])*(closest_x-pos[0]) +
                           (closest_y-pos[1])*(closest_y-pos[1]) +
                           (closest_z-pos[2])*(closest_z-pos[2]));
        if (dist < best_d) { best_d = dist; best = i; }
    }
    return best;
}

int world_to_screen(const float *world, float *out_x, float *out_y, float *out_z) {
    if (!g_camera_valid) return 0;
    D3DXMATRIX view_proj;
    D3DXMatrixMultiply(&view_proj, (const D3DXMATRIX*)&g_view_mat, (const D3DXMATRIX*)&g_proj_mat);
    D3DXVECTOR4 wv((float)world[0], (float)world[1], (float)world[2], 1.0f);
    D3DXVECTOR4 clip;
    D3DXVec4Transform(&clip, &wv, &view_proj);
    if (clip.w == 0.0f) return 0;
    float ndc_x = clip.x / clip.w;
    float ndc_y = clip.y / clip.w;
    if (out_z) *out_z = clip.z / clip.w;
    *out_x = (ndc_x + 1.0f) * 0.5f * (float)g_viewport.Width + (float)g_viewport.X;
    *out_y = (1.0f - ndc_y) * 0.5f * (float)g_viewport.Height + (float)g_viewport.Y;
    return 1;
}

int get_entity_world_pos(int index, float *out_pos) {
    DWORD base = (DWORD)GetModuleHandleA(NULL);
    void *et = *(void**)(base + DAT_ENTITY_TABLE);
    if (!et) return 0;
    int *ent = *(int**)((int)et + 8 + index * 4);
    if (!ent) return 0;
    int gizmo = ent[0x64 / 4];
    if (!gizmo) return 0;
    out_pos[0] = *(float*)(gizmo + 0x54);
    out_pos[1] = *(float*)(gizmo + 0x58);
    out_pos[2] = *(float*)(gizmo + 0x5c);
    return 1;
}

/* 3D axis-aligned gizmo vertex */
typedef struct { float x, y, z; D3DCOLOR color; } GVertex;

void render_editor_overlay(IDirect3DDevice9 *d) {
    if (!g_dev) return;
    
    static int render_log_count = 0;
    if (render_log_count < 3) {
        render_log_count++;
        LOG("render_editor_overlay: called (entities=%d selected=%d camera_valid=%d)",
            g_editor_entity_count, g_selected_count, g_camera_valid);
    }

    int sw = g_sw, sh = g_sh;

    /* Draw top bar indicator */
    {
        DWORD old_fvf;
        d->GetFVF(&old_fvf);
        d->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
        d->SetTexture(0, NULL);
        EVertex bar[4] = {
            {0, 0, 0, 1, 0xCCFF4444},
            {(float)sw, 0, 0, 1, 0xCCFF4444},
            {0, 4, 0, 1, 0xCCFF4444},
            {(float)sw, 4, 0, 1, 0xCCFF4444},
        };
        d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, bar, sizeof(EVertex));
        d->SetFVF(old_fvf);
    }

    /* Draw "EDITOR MODE" text using ImGui */
    if (g_imgui_ready) {
        ImDrawList *dl = ImGui::GetForegroundDrawList();
        char buf[128];
        int y = sh - 28;
        snprintf(buf, sizeof(buf), "EDITOR | Entities: %d | Selected: %d  |  WASD: Move  RMB: Look  E/Q: Up/Down  SHIFT: Fast",
                 g_editor_entity_count, g_selected_count);
        dl->AddText(ImVec2(sw - 10 - 200, 8), 0xFFFF4444, "EDITOR MODE");
        dl->AddText(ImVec2(10, y), 0xFFFF4444, buf);
    }

    /* Draw 3D gizmo and selection indicators */
    if (!g_camera_valid) return;

    /* Save state */
    DWORD old_zenable, old_zwrite, old_ablend;
    DWORD old_srcbl, old_dstbl;
    IDirect3DVertexShader9 *old_vs = NULL;
    IDirect3DPixelShader9 *old_ps = NULL;
    D3DXMATRIX old_world, old_view, old_proj;
    d->GetVertexShader(&old_vs);
    d->GetPixelShader(&old_ps);
    d->GetRenderState(D3DRS_ZENABLE, &old_zenable);
    d->GetRenderState(D3DRS_ZWRITEENABLE, &old_zwrite);
    d->GetRenderState(D3DRS_ALPHABLENDENABLE, &old_ablend);
    d->GetRenderState(D3DRS_SRCBLEND, &old_srcbl);
    d->GetRenderState(D3DRS_DESTBLEND, &old_dstbl);
    d->GetTransform(D3DTS_WORLD, &old_world);
    d->GetTransform(D3DTS_VIEW, &old_view);
    d->GetTransform(D3DTS_PROJECTION, &old_proj);

    d->SetRenderState(D3DRS_ZENABLE, FALSE);
    d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    d->SetVertexShader(NULL);
    d->SetPixelShader(NULL);

    for (int i = 0; i < g_selected_count; i++) {
        float pos[3];
        if (!get_entity_world_pos(g_selected_entities[i], pos)) continue;

        float sx, sy, sd;
        if (!world_to_screen(pos, &sx, &sy, &sd)) continue;

        /* 2D selection circle */
        float radius = 10.0f;
        int steps = 16;
        EVertex cv[17];
        for (int j = 0; j <= steps; j++) {
            float a = (float)j / (float)steps * 6.2831855f;
            cv[j].x = sx + cosf(a) * radius;
            cv[j].y = sy + sinf(a) * radius;
            cv[j].z = 0; cv[j].rhw = 1;
            cv[j].color = 0xFFFF4444;
        }
        d->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
        d->SetTexture(0, NULL);
        d->DrawPrimitiveUP(D3DPT_LINESTRIP, steps, cv, sizeof(EVertex));

        /* 3D axis gizmo (world-space lines drawn in 3D) */
        if (i == 0) {
            float gizmo_size = 0.5f;
            GVertex axis[7] = {
                {pos[0], pos[1], pos[2], 0xFFFF0000},
                {pos[0]+gizmo_size, pos[1], pos[2], 0xFFFF0000},
                {pos[0], pos[1], pos[2], 0xFF00FF00},
                {pos[0], pos[1]+gizmo_size, pos[2], 0xFF00FF00},
                {pos[0], pos[1], pos[2], 0xFF0000FF},
                {pos[0], pos[1], pos[2]+gizmo_size, 0xFF0000FF},
            };

            d->SetTransform(D3DTS_WORLD, &old_world);
            d->SetTransform(D3DTS_VIEW, &g_view_mat);
            d->SetTransform(D3DTS_PROJECTION, &g_proj_mat);

            DWORD old_fvf2;
            d->GetFVF(&old_fvf2);
            d->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
            d->DrawPrimitiveUP(D3DPT_LINELIST, 3, axis, sizeof(GVertex));
            d->SetFVF(old_fvf2);
        }
    }

    /* Restore state */
    d->SetRenderState(D3DRS_ZENABLE, old_zenable);
    d->SetRenderState(D3DRS_ZWRITEENABLE, old_zwrite);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE, old_ablend);
    d->SetRenderState(D3DRS_SRCBLEND, old_srcbl);
    d->SetRenderState(D3DRS_DESTBLEND, old_dstbl);
    d->SetVertexShader(old_vs);
    d->SetPixelShader(old_ps);
    d->SetTransform(D3DTS_WORLD, &old_world);
    d->SetTransform(D3DTS_VIEW, &old_view);
    d->SetTransform(D3DTS_PROJECTION, &old_proj);
    if (old_vs) old_vs->Release();
    if (old_ps) old_ps->Release();
}

void le_send_message(int msg_id, void *data) {
    LOG("le_send_message: entering id=0x%X data=%p le=%p", msg_id, data, g_level_editor);
    void *le = g_level_editor;
    if (!le) { LOG("le_send_message: no LevelEditor"); return; }
    void **vt = *(void***)le;
    if (!vt) { LOG("le_send_message: no vtable"); return; }
    LOG("le_send_message: vtable=%p vt[11]=%p", vt, vt[11]);

    int msg[4] = { 0, 0, 0, (int)data };

    g_v12_our_call = 1;
    typedef void (__thiscall *fn_t)(void* thisptr, int* msg_struct,
                                     int* msg_id_ptr, int unused);
    fn_t fn = (fn_t)vt[11];
    LOG("le_send_message: about to call vtable[11] @ %p", fn);
    fn(le, msg, &msg_id, 0);
    g_v12_our_call = 0;
    LOG("le_send_message(id=0x%X, data=%p) OK", msg_id, data);
}

/* Call the game's real message bus dispatch function
 * Dispatch function at RVA 0x264FC0 (Ghidra 0x00664FC0):
 *   void __thiscall FUN_00664fc0(void *bus, int *msg_id_ptr, void *context)
 * Wrappers set context->field_c = param_1 before calling.
 * Follows the pattern of the existing game wrappers (e.g. FUN_006651d0). */
void game_dispatch_message(int msg_id, int context_param) {
    DWORD base = (DWORD)GetModuleHandleA(NULL);
    void *bus = *(void**)(base + 0xB67A28);
    void *context = *(void**)(base + 0xB67A2C);
    if (!bus) { LOG("dispatch: no bus"); return; }

    if (context) {
        *(int*)((int)context + 0xc) = context_param;
    }

    typedef void (__thiscall *dispatch_t)(void *bus, int *msg_id_ptr, void *context);
    dispatch_t fn = (dispatch_t)(base + 0x264FC0);
    fn(bus, &msg_id, context ? context : &context_param);
}

void set_entity_world_pos(int index, const float *pos) {
    DWORD base = (DWORD)GetModuleHandleA(NULL);
    void *et = *(void**)(base + DAT_ENTITY_TABLE);
    if (!et) return;
    int *ent = *(int**)((int)et + 8 + index * 4);
    if (!ent) return;
    int gizmo = ent[0x64 / 4];
    if (!gizmo) return;
    *(float*)(gizmo + 0x54) = pos[0];
    *(float*)(gizmo + 0x58) = pos[1];
    *(float*)(gizmo + 0x5c) = pos[2];
}

void editor_move_selected(float dx, float dy, float dz) {
    for (int i = 0; i < g_selected_count; i++) {
        float pos[3];
        if (!get_entity_world_pos(g_selected_entities[i], pos)) continue;
        pos[0] += dx; pos[1] += dy; pos[2] += dz;
        set_entity_world_pos(g_selected_entities[i], pos);
    }
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
