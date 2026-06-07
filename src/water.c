#include "water.h"
#include "utils.h"
#include <string.h>
#include <d3d9.h>

/* ================================================================
 * Water Removal — Targeted Binary Patch
 *
 * Patch 1 — Water effect iteration:
 *   Target: 0x0088cfa0 — Water effect iteration function
 *   This function iterates all water effects in the scene and
 *   calls the per-effect renderer (RpWorld_0x0088ce10).
 *   By NOP'ing it, we skip ALL water rendering.
 *
 *   Original bytes: 83 3d 68 66 f7 00 03  (CMP dword ptr [0xf76668], 3)
 *   Patch: C3 90 90 90 90 90 90            (RET + NOP padding)
 *
 * Patch 2 — Water geometry shader selection:
 *   Target: 0x00539ae2 — Conditional branch in water mesh setup
 *   A JZ at this address selects between VERTEX_TRANSFORM_WATER
 *   (flag set) and VERTEX_TRANSFORM (flag clear). By turning the
 *   JZ into an unconditional JMP, water meshes always use the
 *   non-water shader and become invisible/removed.
 *
 *   Original bytes: 74 07  (JZ +7)
 *   Patch: EB 07            (JMP +7 — always take non-water path)
 * ================================================================ */

/* --- Patch 1: Water effects iteration --- */
#define WATER_PATCH_ADDR  0x0088cfa0
#define WATER_PATCH_SIZE  7

static unsigned char g_water_orig[WATER_PATCH_SIZE];
static unsigned char g_water_patch[WATER_PATCH_SIZE] = {0xC3, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};

/* --- Patch 2: Water geometry shader selection --- */
#define WATER_GEOM_PATCH_ADDR  0x00539ae2
#define WATER_GEOM_PATCH_SIZE  2

static unsigned char g_geom_orig[WATER_GEOM_PATCH_SIZE];
static unsigned char g_geom_patched[WATER_GEOM_PATCH_SIZE] = {0xEB, 0x07}; /* JMP +7 */

/* --- Shared state --- */
static int g_water_patched = 0;
static int g_water_enabled = 0;
static int g_blocked_count = 0;

/* ================================================================
 * Apply/Remove the binary patches
 * ================================================================ */

static void apply_water_patch(void) {
    if (g_water_patched) return;
    
    DWORD old;

    /* --- Patch 1: Water effects iteration --- */
    if (!VirtualProtect((LPVOID)WATER_PATCH_ADDR, WATER_PATCH_SIZE, PAGE_EXECUTE_READ, &old)) {
        LOG("Water removal: cannot read effect patch target 0x%08X", WATER_PATCH_ADDR);
        return;
    }
    memcpy(g_water_orig, (void*)WATER_PATCH_ADDR, WATER_PATCH_SIZE);
    VirtualProtect((LPVOID)WATER_PATCH_ADDR, WATER_PATCH_SIZE, old, &old);

    patch_mem(WATER_PATCH_ADDR, g_water_patch, WATER_PATCH_SIZE);

    /* --- Patch 2: Water geometry shader selection --- */
    if (!VirtualProtect((LPVOID)WATER_GEOM_PATCH_ADDR, WATER_GEOM_PATCH_SIZE, PAGE_EXECUTE_READ, &old)) {
        LOG("Water removal: cannot read geometry patch target 0x%08X", WATER_GEOM_PATCH_ADDR);
        return;
    }
    memcpy(g_geom_orig, (void*)WATER_GEOM_PATCH_ADDR, WATER_GEOM_PATCH_SIZE);
    VirtualProtect((LPVOID)WATER_GEOM_PATCH_ADDR, WATER_GEOM_PATCH_SIZE, old, &old);

    patch_mem(WATER_GEOM_PATCH_ADDR, g_geom_patched, WATER_GEOM_PATCH_SIZE);

    g_water_patched = 1;
    LOG("Water removal: patched effect iterator 0x%08X + geometry shader 0x%08X",
        WATER_PATCH_ADDR, WATER_GEOM_PATCH_ADDR);
}

static void remove_water_patch(void) {
    if (!g_water_patched) return;
    
    /* Restore both patches */
    patch_mem(WATER_PATCH_ADDR, g_water_orig, WATER_PATCH_SIZE);
    patch_mem(WATER_GEOM_PATCH_ADDR, g_geom_orig, WATER_GEOM_PATCH_SIZE);

    g_water_patched = 0;
    LOG("Water removal: unpatched 0x%08X + 0x%08X (water rendering restored)",
        WATER_PATCH_ADDR, WATER_GEOM_PATCH_ADDR);
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
    LOG("Water filter: initialized (dual binary patch mode)");
    (void)dev;
}
