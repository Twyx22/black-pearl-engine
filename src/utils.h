#ifndef BPE_UTILS_H
#define BPE_UTILS_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define MOD_NAME "Black Pearl Engine"
#define MOD_VER "v4.0"

extern HINSTANCE g_hinst;

void LOG(const char *fmt, ...);
void patch_mem(DWORD addr, const void *data, size_t len);
int find_text_section(DWORD *out_start, DWORD *out_size);



#endif
