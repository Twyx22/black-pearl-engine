#include "free_camera.h"
#include "utils.h"
#include "menu.h"
#include "input.h"
#include "hooks.h"

#include <math.h>

/* ================================================================
 * Free Camera — D3D9 View/Projection Override
 *
 * Provides a first-person camera controller that replaces the
 * game's D3DTS_VIEW and D3DTS_PROJECTION matrices.  Designed to
 * be called from the existing hk_SetTransform hook in uw.c.
 *
 * State machine:
 *   inactive  → toggle()  → active (cursor hidden, camera detached)
 *   active    → toggle()  → inactive (cursor restored)
 * ================================================================ */

/* -----------------------------------------------------------------
 * Constants
 * ----------------------------------------------------------------- */

/** Base movement speed in world-units per "frame" (≈ 60 Hz). */
#define FREE_CAM_BASE_SPEED   5.0f

/** Mouse sensitivity multiplier. */
#define FREE_CAM_SENSITIVITY  0.003f

/** Maximum pitch angle in radians (≈ ±85°). */
#define FREE_CAM_MAX_PITCH    1.48353f

/** FOV limits (degrees). */
#define FREE_CAM_FOV_MIN      30.0f
#define FREE_CAM_FOV_MAX      120.0f
#define FREE_CAM_FOV_DEFAULT  75.0f

/** Delta-time clamp (seconds) to avoid position jumps on frame stalls. */
#define FREE_CAM_DT_MAX       0.05f

/* -----------------------------------------------------------------
 * Module state
 * ----------------------------------------------------------------- */

typedef struct {
    /* Camera transform */
    D3DXVECTOR3 pos;          /**< World-space position. */
    float       yaw;          /**< Horizontal rotation (rad). */
    float       pitch;        /**< Vertical rotation (rad). */
    float       fov_deg;      /**< Field of view (degrees). */
    float       speed;        /**< Movement speed. */

    /* State flags */
    int         active;       /**< Free camera enabled? */
    int         mouse_look;   /**< Mouse look engaged? (always when active) */

    /* Timing */
    DWORD       last_tick;    /**< Last frame's time for dt calc. */
} FreeCameraState;

static FreeCameraState g_cam;

/* Saved projection parameters (extracted on first call) */
static struct {
    float near_plane;
    float far_plane;
    int   valid;
} g_proj_saved;

/* Mouse delta tracking */
static int g_prev_mx;
static int g_prev_my;
static int g_mouse_tracking;

/* -----------------------------------------------------------------
 * Helpers
 * ----------------------------------------------------------------- */

/** Clamp a value between lo and hi. */
static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/**
 * @brief Extract near / far planes from a D3D perspective projection matrix.
 *
 * D3DXMatrixPerspectiveFovLH layout:
 *   _33 = zf / (zf - zn)
 *   _43 = -zn * zf / (zf - zn)
 *
 * Derivation:
 *   From _33 = zf/(zf-zn) ⇒ _33·zf - _33·zn = zf ⇒ zf·(_33-1) = _33·zn ⇒ zf = _33·zn/(_33-1).
 *   From _43 = -zn·zf/(zf-zn) and noting zf-zn = zf/_33:
 *          _43 = -zn·zf / (zf/_33) = -zn·_33  ⇒  zn = -_43 / _33.
 */
static void extract_znzf(const D3DMATRIX *m, float *zn, float *zf)
{
    float s33 = m->_33;
    float s43 = m->_43;

    /* Guard against division by zero / degenerate matrices. */
    if (fabsf(s33) < 0.0001f || fabsf(s33 - 1.0f) < 0.0001f) {
        *zn = 0.1f;
        *zf = 1000.0f;
        return;
    }

    *zn = -s43 / s33;
    *zf = s33 * (*zn) / (s33 - 1.0f);

    /* Sanity bounds */
    if (*zn < 0.001f) *zn = 0.1f;
    if (*zf < *zn + 1.0f) *zf = *zn + 100.0f;
    if (*zf > 100000.0f) *zf = 100000.0f;
}

/* -----------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------- */

void free_camera_init(void)
{
    g_cam.pos.x      = 0.0f;
    g_cam.pos.y      = 1.5f;
    g_cam.pos.z      = 0.0f;
    g_cam.yaw        = 0.0f;
    g_cam.pitch      = 0.0f;
    g_cam.fov_deg    = FREE_CAM_FOV_DEFAULT;
    g_cam.speed      = FREE_CAM_BASE_SPEED;
    g_cam.active     = 0;
    g_cam.mouse_look = 0;
    g_cam.last_tick  = GetTickCount();

    g_proj_saved.valid = 0;
    g_mouse_tracking    = 0;

    LOG("Free Camera: initialized");
}

void free_camera_toggle(void)
{
    g_cam.active = !g_cam.active;

    if (g_cam.active) {
        /* Hide system cursor and initialise mouse delta tracking. */
        ShowCursor(FALSE);
        g_prev_mx       = g_mouse_x;
        g_prev_my       = g_mouse_y;
        g_mouse_tracking = 1;
        g_cam.mouse_look = 1;
        g_cam.last_tick  = GetTickCount();
        LOG("Free Camera: ENABLED");
    } else {
        ShowCursor(TRUE);
        g_mouse_tracking = 0;
        g_cam.mouse_look = 0;
        LOG("Free Camera: DISABLED");
    }
}

int free_camera_is_active(void)
{
    return g_cam.active;
}

void free_camera_set_fov(float deg)
{
    g_cam.fov_deg = clampf(deg, FREE_CAM_FOV_MIN, FREE_CAM_FOV_MAX);
}

float free_camera_get_fov(void)
{
    return g_cam.fov_deg;
}

void free_camera_update(void)
{
    if (!g_cam.active) return;

    /* ---- Delta time -------------------------------------------------- */
    DWORD now = GetTickCount();
    float dt  = (float)(now - g_cam.last_tick) / 1000.0f;
    if (dt > FREE_CAM_DT_MAX)  dt = FREE_CAM_DT_MAX;
    if (dt < 0.0001f)          dt = 0.016f;   /* fallback ≈ 60 fps */
    g_cam.last_tick = now;

    /* ---- Mouse look -------------------------------------------------- */
    if (g_mouse_tracking) {
        int dx = g_mouse_x - g_prev_mx;
        int dy = g_mouse_y - g_prev_my;
        g_prev_mx = g_mouse_x;
        g_prev_my = g_mouse_y;

        g_cam.yaw   += (float)dx * FREE_CAM_SENSITIVITY;
        g_cam.pitch += (float)dy * FREE_CAM_SENSITIVITY;

        /* Clamp pitch to avoid gimbal lock. */
        g_cam.pitch = clampf(g_cam.pitch, -FREE_CAM_MAX_PITCH, FREE_CAM_MAX_PITCH);
    }

    /* ---- Keyboard movement ------------------------------------------- */
    float forward_x = cosf(g_cam.yaw);
    float forward_z = sinf(g_cam.yaw);

    float right_x   = cosf(g_cam.yaw + D3DX_PI / 2.0f);
    float right_z   = sinf(g_cam.yaw + D3DX_PI / 2.0f);

    float speed = g_cam.speed;
    if (g_key_states[VK_SHIFT])
        speed *= 3.0f;

    float step = speed * dt;

    if (g_key_states['W']) {
        g_cam.pos.x += forward_x * step;
        g_cam.pos.z += forward_z * step;
    }
    if (g_key_states['S']) {
        g_cam.pos.x -= forward_x * step;
        g_cam.pos.z -= forward_z * step;
    }
    if (g_key_states['A']) {
        g_cam.pos.x -= right_x * step;
        g_cam.pos.z -= right_z * step;
    }
    if (g_key_states['D']) {
        g_cam.pos.x += right_x * step;
        g_cam.pos.z += right_z * step;
    }
    if (g_key_states['Q']) {
        g_cam.pos.y += step;
    }
    if (g_key_states['E']) {
        g_cam.pos.y -= step;
    }
}

/* -----------------------------------------------------------------
 * D3D9 transform hooks for SetTransform integration
 * ----------------------------------------------------------------- */

int free_camera_hook_view(D3DMATRIX *out)
{
    if (!g_cam.active || !out)
        return 0;

    /* --- Compute forward vector from yaw / pitch --- */
    D3DXVECTOR3 forward;
    forward.x = cosf(g_cam.yaw) * cosf(g_cam.pitch);
    forward.y = sinf(g_cam.pitch);
    forward.z = sinf(g_cam.yaw) * cosf(g_cam.pitch);
    D3DXVec3Normalize(&forward, &forward);

    /* --- Build look-at target --- */
    D3DXVECTOR3 up = {0.0f, 1.0f, 0.0f};
    D3DXVECTOR3 look_at;
    look_at.x = g_cam.pos.x + forward.x;
    look_at.y = g_cam.pos.y + forward.y;
    look_at.z = g_cam.pos.z + forward.z;

    /* --- Generate LH view matrix --- */
    D3DXMATRIX view;
    D3DXMatrixLookAtLH(&view, &g_cam.pos, &look_at, &up);

    memcpy(out, &view, sizeof(D3DMATRIX));
    return 1;
}

int free_camera_hook_proj(const D3DMATRIX *in, D3DMATRIX *out)
{
    if (!g_cam.active || !in || !out)
        return 0;

    /* Extract near/far from the game's projection matrix (cache on first call). */
    if (!g_proj_saved.valid) {
        extract_znzf(in, &g_proj_saved.near_plane, &g_proj_saved.far_plane);
        g_proj_saved.valid = 1;
        LOG("Free Camera: extracted zn=%.2f zf=%.1f from projection matrix",
            g_proj_saved.near_plane, g_proj_saved.far_plane);
    }

    /* Guard against degenerate cached values. */
    float zn = g_proj_saved.near_plane;
    float zf = g_proj_saved.far_plane;
    if (zn < 0.001f) zn = 0.1f;
    if (zf < zn + 1.0f) zf = zn + 100.0f;

    /* --- Build projection with custom FOV --- */
    float aspect = (g_sw > 0 && g_sh > 0)
                       ? (float)g_sw / (float)g_sh
                       : 16.0f / 9.0f;

    float fov_rad = D3DXToRadian(g_cam.fov_deg);

    D3DXMATRIX proj;
    D3DXMatrixPerspectiveFovLH(&proj, fov_rad, aspect, zn, zf);

    memcpy(out, &proj, sizeof(D3DMATRIX));
    return 1;
}
