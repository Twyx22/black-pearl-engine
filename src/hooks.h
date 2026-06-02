#ifndef BPE_HOOKS_H
#define BPE_HOOKS_H

#include <windows.h>
#include <d3d9.h>

void hook_device(IDirect3DDevice9 *dev);

extern DWORD (WINAPI *real_GetTickCount)(void);
extern BOOL (WINAPI *real_QueryPerformanceCounter)(LARGE_INTEGER*);
extern DWORD g_freeze_tick;
extern LONGLONG g_freeze_perf;
void install_time_hooks(void);
void time_freeze_snapshot(void);

extern HWND g_game_hwnd;
extern int g_imgui_ready;
extern int g_mouse_x, g_mouse_y;
extern int g_mouse_lb, g_mouse_rb;



#endif
