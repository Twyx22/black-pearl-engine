#include "quick_combo.h"
#include "utils.h"
#include "config.h"
#include <string.h>

/* ================================================================
 * Quick Combo (T3.11)
 *
 * Approach:
 *   1. Scan .text for PUSH instructions referencing known combo
 *      string addresses (easy_combos @ 0xBF1AE4,
 *      always_start_combos_first_anim @ 0xBF1AF0).
 *   2. Near each reference, look for a MOV byte [addr], 0/1
 *      instruction that controls the combo flag.
 *   3. Force the flag to 1 (always enabled) via PatchRecord.
 *
 * This makes the combo timer start immediately and remain at
 * maximum, giving the player an effortless max-multiplier.
 * ================================================================ */

/* Offsets from module base for known combo-related strings */
#define QUICK_COMBO_STR1_OFFSET   (EASY_COMBOS_STR - 0x400000)         /* "easy_combos" */
#define QUICK_COMBO_STR2_OFFSET   (ALWAYS_START_COMBOS_FIRST - 0x400000) /* "always_start_combos_first_anim" */

/* Absolute addresses of combo strings (for PUSH scanning) */
static DWORD g_str1_addr = 0;
static DWORD g_str2_addr = 0;

/* Patch storage */
#define MAX_COMBO_PATCHES  8
static PatchRecord g_patches[MAX_COMBO_PATCHES];
static int         g_patch_count = 0;
static int         g_combo_enabled = 0;
static int         g_combo_scanned = 0;

/* Cached AOB results */
static AobCache g_cache_str1 = {0};
static AobCache g_cache_str2 = {0};

/* -----------------------------------------------------------------
 * Helper: NOP a range of bytes at the given address.
 * Returns 1 on success, 0 on failure.
 * ----------------------------------------------------------------- */
static int add_nop_patch(DWORD addr, int size)
{
    if (g_patch_count >= MAX_COMBO_PATCHES) return 0;
    if (size <= 0 || size > 20) return 0;

    PatchRecord *pr = &g_patches[g_patch_count];
    memset(pr, 0, sizeof(PatchRecord));
    pr->addr = addr;
    pr->size = (size_t)size;

    unsigned char nops[20];
    memset(nops, 0x90, (size_t)size);

    if (patch_apply(pr, nops)) {
        g_patch_count++;
        return 1;
    }
    return 0;
}

/* -----------------------------------------------------------------
 * Helper: force-write a single byte at the given address.
 * Returns 1 on success, 0 on failure.
 * ----------------------------------------------------------------- */
static int add_byte_patch(DWORD addr, unsigned char val)
{
    if (g_patch_count >= MAX_COMBO_PATCHES) return 0;

    PatchRecord *pr = &g_patches[g_patch_count];
    memset(pr, 0, sizeof(PatchRecord));
    pr->addr = addr;
    pr->size = 1;

    if (patch_apply(pr, &val)) {
        g_patch_count++;
        return 1;
    }
    return 0;
}

/* -----------------------------------------------------------------
 * Scan .text for PUSH references to a given string address and,
 * near each one, look for a byte flag we can patch.
 * ----------------------------------------------------------------- */
static int scan_string_flags(DWORD str_addr, const char *label)
{
    DWORD text_start, text_size;
    if (!find_text_section(&text_start, &text_size)) {
        LOG("QuickCombo: failed to find .text section");
        return 0;
    }

    unsigned char *code = (unsigned char *)text_start;
    int found = 0;

    for (DWORD i = 0; i + 8 < text_size && g_patch_count < MAX_COMBO_PATCHES; i++) {
        /* Pattern: 68 xx xx xx xx = PUSH imm32 */
        if (code[i] != 0x68) continue;

        DWORD pushed = *(DWORD *)&code[i + 1];
        if (pushed != str_addr) continue;

        DWORD code_addr = text_start + i;
        LOG("QuickCombo: found PUSH of %s (0x%08X) at 0x%08X", label, str_addr, code_addr);

        /* Scan forward up to 80 bytes for a flag write */
        DWORD search_end = i + 80;
        if (search_end > text_size) search_end = text_size;

        for (DWORD j = i; j < search_end; j++) {
            /* Pattern: C6 05 xx xx xx xx 00/01 — MOV byte [addr], 0/1 */
            if (code[j] == 0xC6 && code[j + 1] == 0x05) {
                DWORD flag_addr = *(DWORD *)&code[j + 2];
                int   flag_val  = code[j + 6];

                if ((flag_val == 0 || flag_val == 1) &&
                    flag_addr >= 0x400000 && flag_addr < 0x2000000) {

                    /* Read current value to validate */
                    int cur = *(volatile int *)flag_addr;
                    LOG("QuickCombo: found flag byte at 0x%08X (current=%d) near %s",
                        flag_addr, cur, label);

                    /* Force to 1 if currently 0, otherwise NOP the setter */
                    if (cur == 0) {
                        if (add_byte_patch(flag_addr, 1)) {
                            LOG("QuickCombo: forced flag 0x%08X to 1", flag_addr);
                            found++;
                        }
                    }
                    break; /* one patch per string reference is enough */
                }
            }
        }
    }

    return found;
}

/* -----------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------- */

void quick_combo_apply(void)
{
    if (g_combo_enabled) return;
    if (!g_combo_scanned) {
        DWORD base = (DWORD)GetModuleHandleA(NULL);
        if (!base) {
            LOG("QuickCombo: GetModuleHandleA failed");
            return;
        }

        g_str1_addr = base + QUICK_COMBO_STR1_OFFSET;
        g_str2_addr = base + QUICK_COMBO_STR2_OFFSET;

        LOG("QuickCombo: string addresses resolved — easy_combos=0x%08X, always_start=0x%08X",
            g_str1_addr, g_str2_addr);

        scan_string_flags(g_str1_addr, "easy_combos");
        scan_string_flags(g_str2_addr, "always_start_combos_first_anim");
        g_combo_scanned = 1;
    }

    if (g_patch_count == 0) {
        /* If scanning found nothing, try a fallback AOB scan */
        LOG("QuickCombo: string reference scan found no patches, trying fallback AOB");

        /* Fallback: scan for MOV byte [reg+offset], 0 patterns
         * that look like combo counter resets (88 86 xx xx xx xx) */
        DWORD text_start, text_size;
        if (find_text_section(&text_start, &text_size)) {
            unsigned char *code = (unsigned char *)text_start;
            int fallback_patched = 0;

            for (DWORD i = 0; i + 7 < text_size && g_patch_count < MAX_COMBO_PATCHES && !fallback_patched; i++) {
                /* Pattern: C6 86 xx xx xx xx 00 — MOV byte [esi+offset], 0
                 * (combo counter reset) */
                if (code[i] == 0xC6 && code[i + 1] == 0x86 &&
                    code[i + 6] == 0x00) {
                    DWORD flag_addr = text_start + i;
                    if (add_nop_patch(flag_addr, 7)) {
                        LOG("QuickCombo: NOP'd counter reset at 0x%08X (fallback)", flag_addr);
                        fallback_patched++;
                    }
                }
            }

            if (!fallback_patched) {
                LOG("QuickCombo: fallback AOB found no targets either");
            }
        }
    }

    g_combo_enabled = 1;
    LOG("QuickCombo: activated with %d patch(es)", g_patch_count);
}

void quick_combo_remove(void)
{
    if (!g_combo_enabled) return;

    /* Restore all patches in reverse order */
    for (int i = g_patch_count - 1; i >= 0; i--) {
        patch_restore(&g_patches[i]);
    }
    g_patch_count = 0;
    g_combo_enabled = 0;
    g_combo_scanned = 0; /* allow re-scan on next apply */

    LOG("QuickCombo: deactivated, all patches restored");
}

int quick_combo_get_enabled(void)
{
    return g_combo_enabled;
}
