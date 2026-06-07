#ifndef BPE_WATER_H
#define BPE_WATER_H

#include <windows.h>
#include <d3d9.h>

/* ================================================================
 * Water Removal Module
 *
 * Filters D3D9 draw calls to suppress water rendering.
 * Water in Nu2Api is rendered as large alpha-blended triangle meshes.
 * We track render state and skip DrawIndexedPrimitive calls that
 * match the water rendering signature.
 * ================================================================ */

/* Initialize water filter hooks (called from hook_device) */
void water_init(IDirect3DDevice9 *dev);

/* Enable/disable water removal at runtime */
void water_set_enabled(int enabled);
int  water_get_enabled(void);

/* Stats for debug overlay */
int water_get_blocked_count(void);
void water_reset_blocked_count(void);

#endif
