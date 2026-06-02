#include "utils.h"
#include "hooks.h"
#include "input.h"
#include "config_loader.h"
#include <MinHook.h>

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID reserved) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            g_hinst = hInst;
            DisableThreadLibraryCalls(hInst);
            log_init();
            LOG("=== " MOD_NAME " " MOD_VER " ===");
            if (MH_Initialize() == MH_OK) {
                LOG("MinHook initialized");
            } else {
                LOG("MinHook init failed");
            }
            load_config();
            install_time_hooks();
            MH_EnableHook(MH_ALL_HOOKS);
            return TRUE;
        case DLL_PROCESS_DETACH:
            save_config();
            input_cleanup();
            break;
    }
    return TRUE;
}
