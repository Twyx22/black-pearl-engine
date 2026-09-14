#include "mega_destruct.h"
#include "utils.h"
#include "config.h"
#include "native_cheats.h"
#include <string.h>

/* ================================================================
 * Mega Destruct (T3.12)
 *
 * Strategy:
 *   1. Primary: use native_cheat_set("CHEAT_SELFDESTRUCT", 1)
 *      to trick the game's strcmpi hook.
 *   2. Fallback: scan .text for PUSH references to the
 *      CHEAT_SELFDESTRUCT string (at 0xBF89EC) and look for
 *      flag bytes or conditional branches that control whether
 *      objects can be destroyed.
 * ================================================================ */

#define SELFDESTRUCT_STR_OFFSET   (CHEAT_CAPS_SELFDESTRUCT - 0x400000)  /* "CHEAT_SELFDESTRUCT" @ 0xBF89EC */

static int g_destruct_enabled = 0;
static int g_native_used      = 0;

/* Fallback patch storage */
#define MAX_DESTRUCT_PATCHES  4
static PatchRecord g_patches[MAX_DESTRUCT_PATCHES];
static int         g_patch_count = 0;
static int         g_fallback_scanned = 0;

/* -----------------------------------------------------------------
 * Fallback: scan .text for PUSH of the SELF DESTRUCT string
 * and look for flag bytes or conditional branches nearby.
 * ----------------------------------------------------------------- */
static int scan_fallback_flags(DWORD str_addr)
{
    DWORD text_start, text_size;
    if (!find_text_section(&text_start, &text_size)) {
        LOG("MegaDestruct: failed to find .text section");
        return 0;
    }

    unsigned char *code = (unsigned char *)text_start;
    int found = 0;

    for (DWORD i = 0; i + 8 < text_size && g_patch_count < MAX_DESTRUCT_PATCHES; i++) {
        if (code[i] != 0x68) continue;  /* not PUSH */

        DWORD pushed = *(DWORD *)&code[i + 1];
        if (pushed != str_addr) continue;

        DWORD code_addr = text_start + i;
        LOG("MegaDestruct: found PUSH of CHEAT_SELFDESTRUCT at 0x%08X", code_addr);

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

                    int cur = *(volatile int *)flag_addr;
                    LOG("MegaDestruct: found destruct flag at 0x%08X (current=%d)", flag_addr, cur);

                    if (cur == 0) {
                        PatchRecord pr = {0};
                        pr.addr = flag_addr;
                        pr.size = 1;
                        unsigned char v_on = 1;
                        if (patch_apply(&pr, &v_on)) {
                            g_patches[g_patch_count++] = pr;
                            LOG("MegaDestruct: forced destruct flag 0x%08X to 1", flag_addr);
                            found++;
                            break;
                        }
                    }
                    break;
                }
            }
        }
    }

    return found;
}

/* -----------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------- */

void mega_destruct_apply(void)
{
    if (g_destruct_enabled) return;

    DWORD base = (DWORD)GetModuleHandleA(NULL);

    /* Primary: native cheat */
    if (native_cheat_set("CHEAT_SELFDESTRUCT", 1)) {
        g_native_used = 1;
        g_destruct_enabled = 1;
        LOG("MegaDestruct: activated via native cheat (CHEAT_SELFDESTRUCT)");
        return;
    }

    LOG("MegaDestruct: native cheat unavailable, trying fallback AOB");

    /* Fallback: AOB scan */
    if (!g_fallback_scanned && base) {
        DWORD str_addr = base + SELFDESTRUCT_STR_OFFSET;
        scan_fallback_flags(str_addr);
        g_fallback_scanned = 1;
    }

    /* Fallback 2 disabled: NOP'ing the first CALL in .text hits unrelated
     * code and is destructive. Native cheat + fallback-1 are unaffected. */
    LOG("MegaDestruct: fallback-2 disabled — pattern too generic, skipping");

    g_destruct_enabled = 1;
    LOG("MegaDestruct: activated with %d fallback patch(es)", g_patch_count);
}

void mega_destruct_remove(void)
{
    if (!g_destruct_enabled) return;

    if (g_native_used) {
        native_cheat_set("CHEAT_SELFDESTRUCT", 0);
        g_native_used = 0;
    }

    /* Restore fallback patches */
    for (int i = g_patch_count - 1; i >= 0; i--) {
        patch_restore(&g_patches[i]);
    }
    g_patch_count = 0;
    g_destruct_enabled = 0;

    LOG("MegaDestruct: deactivated");
}

int mega_destruct_get_enabled(void)
{
    return g_destruct_enabled;
}
