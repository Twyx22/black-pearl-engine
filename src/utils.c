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


