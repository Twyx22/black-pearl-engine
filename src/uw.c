#include "uw.h"
#include "utils.h"
#include "menu.h"

#define ASPECT_16_9   1.777777f
#define ASPECT_16_10  1.6f
#define ASPECT_4_3    1.333333f
#define ASPECT_21_9   2.333333f
#define ASPECT_32_9   3.555555f

int g_uw_enabled = 0;
int g_uw_ratio_mode = UW_RATIO_AUTO;
float g_uw_custom_ratio = 2.333333f;

static int g_uw_patched = 0;
static float g_orig_aspect = ASPECT_16_9;

static DWORD g_aspect_addr = 0;
static float g_aspect_saved = 0;

static HRESULT (WINAPI *orig_SetTransform)(IDirect3DDevice9*, D3DTRANSFORMSTATETYPE, const D3DMATRIX*) = NULL;

float uw_get_target_ratio(void)
{
    float actual = (float)g_sw / (float)g_sh;
    switch (g_uw_ratio_mode) {
    case UW_RATIO_16_9: return ASPECT_16_9;
    case UW_RATIO_21_9: return ASPECT_21_9;
    case UW_RATIO_32_9: return ASPECT_32_9;
    case UW_RATIO_CUSTOM: return g_uw_custom_ratio;
    default:
        if (actual > ASPECT_16_9 + 0.01f)
            return actual;
        return ASPECT_16_9;
    }
}

void uw_update_aspect_constant(void)
{
    if (!g_aspect_addr) return;
    float ratio = g_uw_enabled ? uw_get_target_ratio() : g_aspect_saved;
    DWORD old;
    if (VirtualProtect((LPVOID)g_aspect_addr, 4, PAGE_READWRITE, &old)) {
        *(float*)g_aspect_addr = ratio;
        VirtualProtect((LPVOID)g_aspect_addr, 4, old, &old);
    }
}

void uw_apply_patches(void)
{
    if (g_uw_patched) return;
    uw_update_aspect_constant();
    g_uw_patched = 1;
    LOG("Ultra-Wide: patches applied (ratio=%.4f)", uw_get_target_ratio());
}

void uw_remove_patches(void)
{
    if (!g_uw_patched) return;
    if (g_aspect_addr && g_aspect_saved != 0.0f) {
        DWORD old;
        if (VirtualProtect((LPVOID)g_aspect_addr, 4, PAGE_READWRITE, &old)) {
            *(float*)g_aspect_addr = g_aspect_saved;
            VirtualProtect((LPVOID)g_aspect_addr, 4, old, &old);
        }
    }
    g_uw_patched = 0;
    LOG("Ultra-Wide: patches removed");
}

void uw_init(void)
{
    DWORD base = (DWORD)GetModuleHandleA(NULL);
    if (!base) return;

    g_aspect_addr = base + 0x006740b0;

    DWORD old;
    if (VirtualProtect((LPVOID)g_aspect_addr, 4, PAGE_READWRITE, &old)) {
        g_aspect_saved = *(float*)g_aspect_addr;
        VirtualProtect((LPVOID)g_aspect_addr, 4, old, &old);
        LOG("Ultra-Wide: saved original aspect=%.4f from 0x%08X", g_aspect_saved, g_aspect_addr);
    } else {
        g_aspect_addr = 0;
        LOG("Ultra-Wide: failed to read aspect constant");
    }
}

void uw_set_orig_set_transform(HRESULT (WINAPI *fn)(IDirect3DDevice9*, D3DTRANSFORMSTATETYPE, const D3DMATRIX*))
{
    orig_SetTransform = fn;
}

HRESULT WINAPI hk_SetTransform(IDirect3DDevice9 *d, D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix)
{
    if (!orig_SetTransform) {
        return D3DERR_INVALIDCALL;
    }

    if (state == D3DTS_PROJECTION && g_uw_enabled && g_sw > 0 && g_sh > 0) {
        float actual_ratio = (float)g_sw / (float)g_sh;
        float orig_ratio = (g_aspect_saved > 0.1f) ? g_aspect_saved : ASPECT_16_9;

        if (actual_ratio > orig_ratio + 0.01f) {
            D3DMATRIX mod = *matrix;
            float scale = orig_ratio / actual_ratio;
            mod._11 = matrix->_11 * scale;
            LOG("UW: projection fix _11=%.4f * %.4f = %.4f (orig_ratio=%.4f actual=%.4f)",
                matrix->_11, scale, mod._11, orig_ratio, actual_ratio);
            return orig_SetTransform(d, state, &mod);
        } else {
            LOG("UW: projection skip act=%.4f orig=%.4f", actual_ratio, orig_ratio);
        }
    }

    return orig_SetTransform(d, state, matrix);
}

void uw_toggle(void)
{
    g_uw_enabled = !g_uw_enabled;
    if (g_uw_enabled) {
        uw_apply_patches();
    } else {
        uw_remove_patches();
    }
    LOG("Ultra-Wide: toggled %s", g_uw_enabled ? "ON" : "OFF");
}

void uw_set_ratio(int mode)
{
    g_uw_ratio_mode = mode;
    if (g_uw_enabled) {
        uw_update_aspect_constant();
        LOG("Ultra-Wide: ratio mode set to %d (%.4f)", mode, uw_get_target_ratio());
    }
}
