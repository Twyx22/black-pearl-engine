#include "utils.h"

HINSTANCE g_hinst = NULL;

static FILE *g_log = NULL;

void LOG(const char *fmt, ...) {
    if (!g_log) {
        g_log = fopen("bpe.log", "w");
        if (g_log) setvbuf(g_log, NULL, _IONBF, 0);
    }
    if (!g_log) return;
    va_list args;
    va_start(args, fmt);
    fprintf(g_log, "[%lu] ", GetTickCount());
    vfprintf(g_log, fmt, args);
    fprintf(g_log, "\n");
    va_end(args);
}

void patch_mem(DWORD addr, const void *data, size_t len) {
    DWORD old;
    if (VirtualProtect((LPVOID)addr, len, PAGE_EXECUTE_READWRITE, &old)) {
        memcpy((void*)addr, data, len);
        VirtualProtect((LPVOID)addr, len, old, &old);
    }
}

int find_text_section(DWORD *out_start, DWORD *out_size) {
    DWORD base = (DWORD)GetModuleHandleA(NULL);
    if (!base) return 0;

    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)base;
    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)(base + dos->e_lfanew);
    PIMAGE_SECTION_HEADER sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (memcmp(sec[i].Name, ".text", 5) == 0) {
            *out_start = base + sec[i].VirtualAddress;
            *out_size = sec[i].Misc.VirtualSize;
            return 1;
        }
    }
    return 0;
}

int hook_iat_in_module(HMODULE hMod, const char *dll_name, void *real_fn, void *hook_fn) {
    if (!hMod || !real_fn) return 0;
    DWORD base = (DWORD)hMod;

    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;

    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;

    DWORD import_rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!import_rva) return 0;

    PIMAGE_IMPORT_DESCRIPTOR import = (PIMAGE_IMPORT_DESCRIPTOR)(base + import_rva);
    int hooked = 0;

    while (import->Name) {
        const char *name = (const char*)(base + import->Name);
        if (_stricmp(name, dll_name) == 0) {
            PIMAGE_THUNK_DATA thunk = (PIMAGE_THUNK_DATA)(base + import->FirstThunk);
            while (thunk->u1.Function) {
                if ((DWORD)thunk->u1.Function == (DWORD)real_fn) {
                    DWORD old;
                    if (VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &old)) {
                        thunk->u1.Function = (ULONG_PTR)hook_fn;
                        VirtualProtect(&thunk->u1.Function, sizeof(void*), old, &old);
                        hooked++;
                    }
                }
                thunk++;
            }
        }
        import++;
    }
    return hooked;
}

void hook_iat_function_all(const char *dll_name, const char *func_name, void *hook_fn, void **real_fn_out) {
    if (!*real_fn_out) {
        HMODULE hMod = GetModuleHandleA(dll_name);
        if (hMod) {
            *real_fn_out = (void*)GetProcAddress(hMod, func_name);
        }
    }
    if (!*real_fn_out) return;

    HMODULE hMain = GetModuleHandleA(NULL);
    int total = hook_iat_in_module(hMain, dll_name, *real_fn_out, hook_fn);

    HMODULE mods[256];
    DWORD needed;
    HANDLE hProc = GetCurrentProcess();
    if (EnumProcessModules(hProc, mods, sizeof(mods), &needed)) {
        int count = needed / sizeof(HMODULE);
        for (int i = 0; i < count; i++) {
            if (mods[i] != hMain) {
                total += hook_iat_in_module(mods[i], dll_name, *real_fn_out, hook_fn);
            }
        }
    }

    if (total > 0) {
        LOG("%s hooked in %d location(s)", func_name, total);
    }
}

void hook_iat_function(const char *dll_name, const char *func_name, void *hook_fn, void **real_fn) {
    if (*real_fn) return;
    HMODULE hMod = GetModuleHandleA(dll_name);
    if (!hMod) return;
    *real_fn = (void*)GetProcAddress(hMod, func_name);
    if (!*real_fn) return;

    DWORD base = (DWORD)GetModuleHandleA(NULL);
    if (!base) return;

    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)base;
    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)(base + dos->e_lfanew);
    PIMAGE_IMPORT_DESCRIPTOR import = (PIMAGE_IMPORT_DESCRIPTOR)(base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

    while (import->Name) {
        const char *name = (const char*)(base + import->Name);
        if (_stricmp(name, dll_name) == 0) {
            PIMAGE_THUNK_DATA thunk = (PIMAGE_THUNK_DATA)(base + import->FirstThunk);
            while (thunk->u1.Function) {
                if ((DWORD)thunk->u1.Function == (DWORD)*real_fn) {
                    DWORD old;
                    if (VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &old)) {
                        thunk->u1.Function = (ULONG_PTR)hook_fn;
                        VirtualProtect(&thunk->u1.Function, sizeof(void*), old, &old);
                        LOG("%s hooked", func_name);
                    }
                }
                thunk++;
            }
        }
        import++;
    }
}
