#include "water.h"
#include "utils.h"
#include <string.h>
#include <d3d9.h>

/* ================================================================
 * Water Removal — Targeted Binary Patch
 *
 * Target: 0x0088cfa0 — Water effect iteration function
 *   This function iterates all water effects in the scene and
 *   calls the per-effect renderer (RpWorld_0x0088ce10).
 *   By NOP'ing it, we skip ALL water rendering.
 *
 * Original bytes: 83 3d 68 66 f7 00 03  (CMP dword ptr [0xf76668], 3)
 * Patch: C3 90 90 90 90 90 90            (RET + NOP padding)
 * ================================================================ */

#define WATER_PATCH_ADDR  0x0088cfa0
#define WATER_PATCH_SIZE  7

static unsigned char g_water_orig[WATER_PATCH_SIZE];
static unsigned char g_water_patch[WATER_PATCH_SIZE] = {0xC3, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
static int g_water_patched = 0;
static int g_water_enabled = 0;
static int g_blocked_count = 0;

/* ================================================================
 * Apply/Remove the binary patch
 * ================================================================ */

static void apply_water_patch(void) {
    if (g_water_patched) return;
    
    /* Save original bytes (with read guard) */
    DWORD old;
    if (!VirtualProtect((LPVOID)WATER_PATCH_ADDR, WATER_PATCH_SIZE, PAGE_EXECUTE_READ, &old)) {
        LOG("Water removal: cannot read patch target 0x%08X", WATER_PATCH_ADDR);
        return;
    }
    memcpy(g_water_orig, (void*)WATER_PATCH_ADDR, WATER_PATCH_SIZE);
    VirtualProtect((LPVOID)WATER_PATCH_ADDR, WATER_PATCH_SIZE, old, &old);
    
    /* Apply RET patch */
    patch_mem(WATER_PATCH_ADDR, g_water_patch, WATER_PATCH_SIZE);
    g_water_patched = 1;
    LOG("Water removal: patched 0x%08X (skips water effect iteration)", WATER_PATCH_ADDR);
}

static void remove_water_patch(void) {
    if (!g_water_patched) return;
    
    /* Restore original bytes */
    patch_mem(WATER_PATCH_ADDR, g_water_orig, WATER_PATCH_SIZE);
    g_water_patched = 0;
    LOG("Water removal: unpatched 0x%08X (water rendering restored)", WATER_PATCH_ADDR);
}

/* ================================================================
 * Public API (same interface as before for compatibility)
 * ================================================================ */

void water_set_enabled(int enabled) {
    g_water_enabled = enabled;
    if (enabled) {
        apply_water_patch();
    } else {
        remove_water_patch();
    }
}

int water_get_enabled(void) {
    return g_water_enabled;
}

int water_get_blocked_count(void) {
    return g_blocked_count;
}

void water_reset_blocked_count(void) {
    g_blocked_count = 0;
}

/* ================================================================
 * Initialization (called from hook_device)
 * ================================================================ */

void water_init(IDirect3DDevice9 *dev) {
    LOG("Water filter: initialized (binary patch mode)");
    (void)dev;
}
