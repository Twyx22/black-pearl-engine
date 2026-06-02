#ifndef BPE_UW_H
#define BPE_UW_H

#include <windows.h>
#include <d3d9.h>
#include <d3dx9math.h>

#define UW_RATIO_AUTO 0
#define UW_RATIO_16_9 1
#define UW_RATIO_21_9 2
#define UW_RATIO_32_9 3
#define UW_RATIO_CUSTOM 4

extern int g_uw_enabled;
extern int g_uw_ratio_mode;
extern float g_uw_custom_ratio;

float uw_get_target_ratio(void);
void uw_apply_patches(void);
void uw_remove_patches(void);
void uw_update_aspect_constant(void);
void uw_init(void);
void uw_set_orig_set_transform(HRESULT (WINAPI *fn)(IDirect3DDevice9*, D3DTRANSFORMSTATETYPE, const D3DMATRIX*));
void uw_toggle(void);
void uw_set_ratio(int mode);

HRESULT WINAPI hk_SetTransform(IDirect3DDevice9 *d, D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix);

#endif
