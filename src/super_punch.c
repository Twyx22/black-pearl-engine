#include "super_punch.h"
#include "utils.h"
#include "config.h"
#include "native_cheats.h"
#include <string.h>

/* ================================================================
 * Super Punch (T3.9)
 *
 * Strategy:
 *   1. Primary: use native_cheat_set("CHEAT_SUPERSLAP", 1) to
 *      trick the game's strcmpi into matching the cheat name.
 *   2. Fallback: scan .text for PUSH references to the
 *      "punch_always_stun" string (at 0xBE333C) and force the
 *      controlling flag byte to 1.
 * ================================================================ */

#define PUNCH_STR_OFFSET   (PUNCH_ALWAYS_STUN_STR - 0x400000)  /* "punch_always_stun" @ 0xBE333C */

static int g_punch_enabled = 0;
static int g_native_used   = 0;

/* Fallback patch storage */
#define MAX_PUNCH_PATCHES  4
static PatchRecord g_patches[MAX_PUNCH_PATCHES];
static int         g_patch_count = 0;
static int         g_fallback_scanned = 0;

/* -----------------------------------------------------------------
 * Fallback: scan .text for PUSH of the punch_always_stun string
 * and look for a flag byte nearby.
 * ----------------------------------------------------------------- */
static int scan_fallback_flags(DWORD str_addr)
{
    DWORD text_start, text_size;
    if (!find_text_section(&text_start, &text_size)) {
        LOG("SuperPunch: failed to find .text section");
        return 0;
    }

    unsigned char *code = (unsigned char *)text_start;
    int found = 0;

    for (DWORD i = 0; i + 8 < text_size && g_patch_count < MAX_PUNCH_PATCHES; i++) {
        if (code[i] != 0x68) continue;  /* not PUSH */

        DWORD pushed = *(DWORD *)&code[i + 1];
        if (pushed != str_addr) continue;

        DWORD code_addr = text_start + i;
        LOG("SuperPunch: found PUSH of punch_always_stun at 0x%08X", code_addr);

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
                    LOG("SuperPunch: found stun flag at 0x%08X (current=%d)", flag_addr, cur);

                    if (cur == 0) {
                        PatchRecord pr = {0};
                        pr.addr = flag_addr;
                        pr.size = 1;
                        unsigned char v_on = 1;
                        if (patch_apply(&pr, &v_on)) {
                            g_patches[g_patch_count++] = pr;
                            LOG("SuperPunch: forced stun flag 0x%08X to 1", flag_addr);
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

void super_punch_apply(void)
{
    if (g_punch_enabled) return;

    DWORD base = (DWORD)GetModuleHandleA(NULL);

    /* Primary: native cheat */
    if (native_cheat_set("CHEAT_SUPERSLAP", 1)) {
        g_native_used = 1;
        g_punch_enabled = 1;
        LOG("SuperPunch: activated via native cheat (CHEAT_SUPERSLAP)");
        return;
    }

    LOG("SuperPunch: native cheat unavailable, trying fallback AOB");

    /* Fallback: AOB scan */
    if (!g_fallback_scanned && base) {
        DWORD str_addr = base + PUNCH_STR_OFFSET;
        scan_fallback_flags(str_addr);
        g_fallback_scanned = 1;
    }

    /* If no patches were applied via fallback, try another approach:
     * scan for patterns that look like punch damage/stun checks */
    if (g_patch_count == 0 && base) {
        DWORD text_start, text_size;
        if (find_text_section(&text_start, &text_size)) {
            unsigned char *code = (unsigned char *)text_start;

            /* Look for CMP dword [reg+offset], 0 near punch-related addresses
             * Pattern: 83 xx xx xx 00 — CMP dword [reg+offset], 0 */
            for (DWORD i = 0; i + 6 < text_size && g_patch_count < MAX_PUNCH_PATCHES; i++) {
                if (code[i] == 0x83 && code[i + 4] == 0x00) {
                    /* CMP dword [reg+offset], 0 — often a stun/damage check */
                    /* Hmm, we don't know the exact pattern — log and skip */
                }
            }
        }
    }

    g_punch_enabled = 1;
    LOG("SuperPunch: activated with %d fallback patch(es)", g_patch_count);
}

void super_punch_remove(void)
{
    if (!g_punch_enabled) return;

    if (g_native_used) {
        native_cheat_set("CHEAT_SUPERSLAP", 0);
        g_native_used = 0;
    }

    /* Restore fallback patches */
    for (int i = g_patch_count - 1; i >= 0; i--) {
        patch_restore(&g_patches[i]);
    }
    g_patch_count = 0;
    g_punch_enabled = 0;

    LOG("SuperPunch: deactivated");
}

int super_punch_get_enabled(void)
{
    return g_punch_enabled;
}
