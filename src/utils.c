#include "utils.h"

HINSTANCE g_hinst = NULL;

static FILE *g_log = NULL;

/* ================================================================== */
/*  Logging                                                            */
/* ================================================================== */

/** Initialise the debug console and logging subsystem. */
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

/** Write a timestamped message to both bpe.log and the debug console. */
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

/* ================================================================== */
/*  Low-level memory patching                                          */
/* ================================================================== */

/** Apply len bytes of data at addr, changing page protection to RW. */
void patch_mem(DWORD addr, const void *data, size_t len) {
    DWORD old;
    if (VirtualProtect((LPVOID)addr, len, PAGE_EXECUTE_READWRITE, &old)) {
        memcpy((void*)addr, data, len);
        VirtualProtect((LPVOID)addr, len, old, &old);
    }
}

/* ================================================================== */
/*  Safe read / write                                                   */
/* ================================================================== */

/**
 * @brief Write data to an arbitrary address, temporarily making the page
 *        writable.
 * @param addr  Target address.
 * @param data  Source buffer.
 * @param len   Number of bytes to write.
 * @return 1 on success, 0 on failure (logs the error internally).
 */
int safe_write(DWORD addr, const void *data, size_t len) {
    DWORD old;
    if (!VirtualProtect((LPVOID)addr, len, PAGE_EXECUTE_READWRITE, &old)) {
        LOG("safe_write: VirtualProtect(RW) failed at 0x%08X (len=%zu)", addr, len);
        return 0;
    }
    memcpy((void*)addr, data, len);
    VirtualProtect((LPVOID)addr, len, old, &old);
    return 1;
}

/**
 * @brief Read data from an arbitrary address, temporarily making the page
 *        readable (code pages are normally readable, but this ensures it).
 * @param addr Source address.
 * @param out  Destination buffer.
 * @param len  Number of bytes to read.
 * @return 1 on success, 0 on failure.
 */
int safe_read(DWORD addr, void *out, size_t len) {
    DWORD old;
    if (!VirtualProtect((LPVOID)addr, len, PAGE_EXECUTE_READWRITE, &old)) {
        LOG("safe_read: VirtualProtect(RW) failed at 0x%08X (len=%zu)", addr, len);
        return 0;
    }
    memcpy(out, (void*)addr, len);
    VirtualProtect((LPVOID)addr, len, old, &old);
    return 1;
}

/* ================================================================== */
/*  PatchRecord – save-apply-restore lifecycle                         */
/* ================================================================== */

/**
 * @brief Apply a patch, saving the original bytes first.
 *
 * Does nothing if the record is already active.
 *
 * @param pr        Pointer to an initialised PatchRecord.
 * @param new_bytes Buffer containing the replacement bytes (must be at
 *                  least pr->size bytes).
 * @return 1 on success, 0 on failure (logs internally).
 */
int patch_apply(PatchRecord *pr, const void *new_bytes) {
    if (!pr || !new_bytes || pr->size == 0 || pr->addr == 0) {
        LOG("patch_apply: invalid arguments");
        return 0;
    }
    if (pr->active) {
        LOG("patch_apply: already active at 0x%08X", pr->addr);
        return 1; /* idempotent — already applied */
    }

    /* Save originals first (with read guard) */
    DWORD old;
    if (!VirtualProtect((LPVOID)pr->addr, pr->size, PAGE_EXECUTE_READWRITE, &old)) {
        LOG("patch_apply: VirtualProtect(RW) failed at 0x%08X (size=%zu)",
            pr->addr, pr->size);
        return 0;
    }

    pr->orig_bytes = (unsigned char *)malloc(pr->size);
    if (!pr->orig_bytes) {
        LOG("patch_apply: malloc(%zu) failed", pr->size);
        VirtualProtect((LPVOID)pr->addr, pr->size, old, &old);
        return 0;
    }

    memcpy(pr->orig_bytes, (void*)pr->addr, pr->size);
    memcpy((void*)pr->addr, new_bytes, pr->size);
    VirtualProtect((LPVOID)pr->addr, pr->size, old, &old);

    pr->active = 1;
    LOG("patch_apply: patched %zu byte(s) at 0x%08X", pr->size, pr->addr);
    return 1;
}

/**
 * @brief Restore the original bytes that were saved by patch_apply().
 *
 * Frees the internal heap buffer and clears the active flag.
 * Does nothing if the record is not active.
 *
 * @param pr Pointer to an active PatchRecord.
 * @return 1 on success, 0 on failure.
 */
int patch_restore(PatchRecord *pr) {
    if (!pr || !pr->active || !pr->orig_bytes) {
        return 1; /* nothing to do */
    }

    DWORD old;
    if (!VirtualProtect((LPVOID)pr->addr, pr->size, PAGE_EXECUTE_READWRITE, &old)) {
        LOG("patch_restore: VirtualProtect(RW) failed at 0x%08X (size=%zu)",
            pr->addr, pr->size);
        return 0;
    }

    memcpy((void*)pr->addr, pr->orig_bytes, pr->size);
    VirtualProtect((LPVOID)pr->addr, pr->size, old, &old);

    free(pr->orig_bytes);
    pr->orig_bytes = NULL;
    pr->active = 0;

    LOG("patch_restore: restored %zu byte(s) at 0x%08X", pr->size, pr->addr);
    return 1;
}

/**
 * @brief Free the snapshot buffer without touching the target memory.
 *
 * Safe to call on a zero-initialised or already-freed record.
 * Leaves the record in a clean {0} state.
 *
 * @param pr Pointer to a PatchRecord.
 */
void patch_free(PatchRecord *pr) {
    if (pr) {
        free(pr->orig_bytes);
        pr->orig_bytes = NULL;
        pr->size = 0;
        pr->addr = 0;
        pr->active = 0;
    }
}

/* ================================================================== */
/*  .text section discovery                                            */
/* ================================================================== */

/**
 * @brief Locate the .text section of the current module.
 *
 * @param out_start Receives the virtual address of the section start.
 * @param out_size  Receives the virtual size of the section.
 * @return 1 on success, 0 on failure.
 */
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

/* ================================================================== */
/*  AOB pattern scanning                                                */
/* ================================================================== */

/** Locate a byte pattern in the .text section.
 *
 * @param pattern    Byte values to match (wildcards are '?' in mask).
 * @param mask       Per-byte specifier: 'x' = must match, '?' = wildcard.
 * @param pattern_len Number of bytes to scan (uses strlen(mask) when
 *                   this is larger than mask length).
 * @return Virtual address of the first match, or 0 on failure.
 */
DWORD find_pattern(const unsigned char *pattern, const char *mask,
                   size_t pattern_len) {
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

/* ================================================================== */
/*  Cached AOB scanner                                                  */
/* ================================================================== */

/**
 * @brief Like find_pattern() but caches the result so repeated calls
 *        with the same cache entry are nearly free.
 *
 * @param cache  Pointer to an AobCache (zero-init before first use).
 * @return The virtual address of the match, or 0.
 */
DWORD find_pattern_cached(AobCache *cache) {
    if (!cache) return 0;

    if (cache->scanned) {
        return cache->result;
    }

    cache->result = find_pattern(cache->pattern, cache->mask, cache->len);
    cache->scanned = 1;
    return cache->result;
}

void bpe_path(char *out, size_t sz, const char *file) {
    if (!out || sz == 0 || !file) return;
    char mod[MAX_PATH];
    DWORD n = GetModuleFileNameA(g_hinst, mod, sizeof(mod));
    if (n == 0 || n >= sizeof(mod)) {
        snprintf(out, sz, "%s", file);
        return;
    }
    char *sep = strrchr(mod, '\\');
    if (!sep) sep = strrchr(mod, '/');
    if (!sep) {
        snprintf(out, sz, "%s", file);
        return;
    }
    *sep = '\0';
    snprintf(out, sz, "%s\\%s", mod, file);
}
