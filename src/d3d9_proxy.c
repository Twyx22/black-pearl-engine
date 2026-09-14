#include "utils.h"
#include "hooks.h"
#include "input.h"
#include "config_loader.h"
#include "native_cheats.h"
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
            native_cheats_init();
            MH_EnableHook(MH_ALL_HOOKS);
            return TRUE;
        case DLL_PROCESS_DETACH:
            hooks_cleanup();
            MH_DisableHook(MH_ALL_HOOKS);
            MH_RemoveHook(MH_ALL_HOOKS);
            MH_Uninitialize();
            save_config();
            input_cleanup();
            break;
    }
    return TRUE;
}
