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
void hooks_cleanup(void);

#endif
