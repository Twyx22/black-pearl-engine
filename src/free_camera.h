#ifndef BPE_FREE_CAMERA_H
#define BPE_FREE_CAMERA_H

#include <windows.h>
#include <d3d9.h>
#include <d3dx9math.h>

/* ================================================================
 * Free Camera Module
 *
 * Hooks the D3D9 view and projection transform chain to provide
 * a freely-controllable third/first-person camera via WASD + mouse.
 *
 * Controls (when active):
 *   W/S       Move forward/backward (horizontal plane)
 *   A/D       Strafe left/right
 *   Q/E       Move up/down
 *   Mouse     Look around (yaw/pitch)
 *   Shift     Speed boost (3x)
 *
 * Integration: call free_camera_hook_view() and
 * free_camera_hook_proj() from within the existing
 * hk_SetTransform (uw.c) when the respective state type is
 * seen and free_camera_is_active() returns non-zero.
 * ================================================================ */

/* ---- Lifecycle ------------------------------------------------- */

/** Initialise the camera state (safe to call multiple times). */
void free_camera_init(void);

/** Enable / disable the free camera (hides / shows the cursor). */
void free_camera_toggle(void);

/** Query whether the free camera is currently active. */
int free_camera_is_active(void);

/** Set the FOV in degrees (clamped to [30, 120]). */
void free_camera_set_fov(float deg);

/** Get the current FOV in degrees. */
float free_camera_get_fov(void);

/* ---- Per-frame ------------------------------------------------- */

/**
 * @brief Update camera position/orientation from keyboard & mouse input.
 *
 * Must be called once per frame while the free camera is active.
 * Intended to be called from update_cheats() or menu_update_input().
 */
void free_camera_update(void);

/* ---- D3D9 transform hooks (call from hk_SetTransform) ---------- */

/**
 * @brief Compute the view matrix for the current camera state.
 *
 * @param out  Receives the D3DTS_VIEW matrix to pass to
 *             orig_SetTransform().
 * @return 1 if the view was overridden, 0 if the caller should
 *         pass through the original matrix.
 */
int free_camera_hook_view(D3DMATRIX *out);

/**
 * @brief Adjust the projection matrix for the active FOV setting.
 *
 * Extracts near/far planes from the game's original projection
 * and rewrites the matrix with the user's FOV.
 *
 * @param in   The original D3DTS_PROJECTION matrix.
 * @param out  Receives the modified matrix.
 * @return 1 if the projection was modified, 0 to pass through.
 */
int free_camera_hook_proj(const D3DMATRIX *in, D3DMATRIX *out);

#endif /* BPE_FREE_CAMERA_H */
