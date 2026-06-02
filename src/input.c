#include "input.h"
#include "utils.h"
#include "cheats.h"
#include "menu.h"
#include "hooks.h"

int g_key_states[256] = {0};
int g_keys[256] = {0};

static HHOOK g_kb_hook = NULL;
static HHOOK g_kb_ll_hook = NULL;
static int g_input_hooks_installed = 0;
static int g_input_hook_attempts = 0;
static int g_hook_log_count = 0;

typedef HRESULT (WINAPI *DirectInput8Create_t)(HINSTANCE, DWORD, REFIID, LPVOID*, void*);
static DirectInput8Create_t real_DirectInput8Create = NULL;

#define DI8_VT_CREATE_DEVICE 3

#define DIDEV_VT_GET_DEVICE_STATE 9
#define DIDEV_VT_ACQUIRE 7
#define DIDEV_VT_UNACQUIRE 8

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
        return 0;
    }
    return orig_GetDeviceState(dev, size, data);
}

static HRESULT WINAPI hk_Acquire(void *dev) {
    if (g_menu_open) {
        if (g_hook_log_count < 15) {
            g_hook_log_count++;
            LOG("DirectInput Acquire blocked");
        }
        return 0;
    }
    return orig_Acquire(dev);
}

static HRESULT WINAPI hk_Unacquire(void *dev) {
    return orig_Unacquire(dev);
}

static HRESULT WINAPI hk_CreateDevice_di(void *di, REFGUID rguid, void **ppDevice, void *unk) {
    HRESULT hr = orig_CreateDevice_di(di, rguid, ppDevice, unk);
    if (SUCCEEDED(hr) && ppDevice && *ppDevice) {
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

static LRESULT CALLBACK hk_keyboard_proc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && g_menu_open) {
        return 1;
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
            return 1;
        }
    }
    return CallNextHookEx(g_kb_ll_hook, nCode, wParam, lParam);
}

void install_input_hooks(void) {
    if (g_input_hooks_installed) return;
    if (g_input_hook_attempts > 5) return;
    g_input_hook_attempts++;

    LOG("Installing input hooks (attempt %d)...", g_input_hook_attempts);

    hook_iat_function_all("dinput8.dll", "DirectInput8Create", (void*)hk_DirectInput8Create, (void**)&real_DirectInput8Create);

    if (!g_kb_hook && g_hinst) {
        DWORD tid = GetCurrentThreadId();
        g_kb_hook = SetWindowsHookEx(WH_KEYBOARD, hk_keyboard_proc, g_hinst, tid);
        if (g_kb_hook) {
            LOG("WH_KEYBOARD hook installed (tid=%lu)", tid);
        } else {
            LOG("WH_KEYBOARD hook failed (err=%lu)", GetLastError());
        }
    }

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

void input_cleanup(void) {
    if (g_kb_hook) { UnhookWindowsHookEx(g_kb_hook); g_kb_hook = NULL; }
    if (g_kb_ll_hook) { UnhookWindowsHookEx(g_kb_ll_hook); g_kb_ll_hook = NULL; }
    if (g_fake_kb_vt) { VirtualFree(g_fake_kb_vt, 0, MEM_RELEASE); g_fake_kb_vt = NULL; }
    if (g_fake_di8_vt) { VirtualFree(g_fake_di8_vt, 0, MEM_RELEASE); g_fake_di8_vt = NULL; }
}
