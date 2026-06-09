#include "utils.h"

HINSTANCE g_hinst = NULL;

static FILE *g_log = NULL;

void log_init(void) {
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
    SetConsoleTitleA(MOD_NAME " " MOD_VER " - Debug Console");
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h) {
        CONSOLE_CURSOR_INFO ci = { 1, FALSE };
        SetConsoleCursorInfo(h, &ci);
    }
    LOG("Console initialized");
}

void LOG(const char *fmt, ...) {
    if (!g_log) {
        g_log = fopen("bpe.log", "w");
        if (g_log) setvbuf(g_log, NULL, _IONBF, 0);
    }
    char buf[4096];
    int len = snprintf(buf, sizeof(buf), "[%lu] ", GetTickCount());
    va_list args;
    va_start(args, fmt);
    len += vsnprintf(buf + len, sizeof(buf) - len, fmt, args);
    va_end(args);
    if (len < 0) {
        len = 0;
    } else if (len >= (int)sizeof(buf) - 2) {
        len = (int)sizeof(buf) - 2;
    }
    buf[len] = '\n';
    buf[len + 1] = '\0';

    if (g_log) {
        fputs(buf, g_log);
        fflush(g_log);
    }
    WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), buf, strlen(buf), NULL, NULL);
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

DWORD find_pattern(const unsigned char *pattern, const char *mask, size_t pattern_len) {
    DWORD text_start, text_size;
    if (!find_text_section(&text_start, &text_size)) {
        return 0;
    }
    unsigned char *code = (unsigned char*)text_start;
    size_t mask_len = strlen(mask);
    if (mask_len < pattern_len) pattern_len = mask_len;
    
    for (DWORD i = 0; i + pattern_len <= text_size; i++) {
        int found = 1;
        for (size_t j = 0; j < pattern_len; j++) {
            if (mask[j] == 'x' && code[i + j] != pattern[j]) {
                found = 0;
                break;
            }
        }
        if (found) {
            return text_start + i;
        }
    }
    return 0;
}


