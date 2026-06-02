#ifndef BPE_HOOKS_H
#define BPE_HOOKS_H

#include <windows.h>
#include <d3d9.h>

void hook_device(IDirect3DDevice9 *dev);
void hook_peekmessage(void);

extern DWORD (WINAPI *real_GetTickCount)(void);
extern BOOL (WINAPI *real_QueryPerformanceCounter)(LARGE_INTEGER*);
extern DWORD g_freeze_tick;
extern LONGLONG g_freeze_perf;
void install_time_hooks(void);
void time_freeze_snapshot(void);

void install_level_editor_hook(void);
void install_v12_hook(void);
void* get_level_editor(void);
void le_send_message(int msg_id, void *data);
void scan_entity_table(void);
void render_editor_overlay(IDirect3DDevice9 *d);
void editor_clear_selection(void);
void editor_select_entity(int index);
void hooks_cleanup(void);

extern int g_v12_our_call;
extern int g_editor_enabled;
extern int g_editor_entity_count;
extern int g_editor_auto_cycle;
extern int g_mouse_x, g_mouse_y;
extern int g_mouse_lb, g_mouse_rb;

extern D3DVIEWPORT9 g_viewport;
extern D3DMATRIX g_view_mat, g_proj_mat;
extern int g_camera_valid;

/* Editor free-fly camera */
extern D3DMATRIX g_editor_view;
extern float g_editor_cam_pos[3];
extern float g_editor_cam_yaw, g_editor_cam_pitch;
extern int g_editor_cam_initialized;
extern int g_settransform_log_count;
extern int g_shader_const_log_count;
extern int g_proj_captured;
extern int g_extra_saved;
void editor_cam_init(void);
void editor_cam_update(void);
void editor_scan_camera_pos(const float *cam_pos);

int world_to_screen(const float *world, float *out_x, float *out_y, float *out_z);
int get_entity_world_pos(int index, float *out_pos);
int screen_to_world_ray(float screen_x, float screen_y,
                         float *out_origin, float *out_dir);
int pick_entity_at_screen(float screen_x, float screen_y);

void game_dispatch_message(int msg_id, int context_param);
void set_entity_world_pos(int index, const float *pos);
void editor_move_selected(float dx, float dy, float dz);

#endif
