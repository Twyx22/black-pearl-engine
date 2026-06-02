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

int hook_iat_in_module(HMODULE hMod, const char *dll_name, void *real_fn, void *hook_fn);
void hook_iat_function_all(const char *dll_name, const char *func_name, void *hook_fn, void **real_fn_out);
void hook_iat_function(const char *dll_name, const char *func_name, void *hook_fn, void **real_fn);

#endif
