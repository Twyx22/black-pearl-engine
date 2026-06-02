#include "utils.h"
#include "hooks.h"
#include "input.h"
#include "config_loader.h"

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID reserved) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            g_hinst = hInst;
            DisableThreadLibraryCalls(hInst);
            LOG("=== " MOD_NAME " " MOD_VER " ===");
            load_config();
            hook_peekmessage();
            install_time_hooks();
            return TRUE;
        case DLL_PROCESS_DETACH:
            save_config();
            input_cleanup();
            hooks_cleanup();
            break;
    }
    return TRUE;
}
