#include "utils.h"
#include "hooks.h"
#include "input.h"

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID reserved) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            g_hinst = hInst;
            DisableThreadLibraryCalls(hInst);
            LOG("=== " MOD_NAME " " MOD_VER " ===");
            hook_peekmessage();
            install_time_hooks();
            return TRUE;
        case DLL_PROCESS_DETACH:
            input_cleanup();
            hooks_cleanup();
            break;
    }
    return TRUE;
}
