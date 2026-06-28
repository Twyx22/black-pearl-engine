#include "infinite_cannonballs.h"
#include "utils.h"
#include "config.h"
#include "native_cheats.h"
#include <string.h>

/* ================================================================
 * Infinite Cannonballs (T3.13)
 *
 * Strategy:
 *   1. Primary: use native_cheat_set("CHEAT_INFINITE_TORPEDOS", 1)
 *      to trick the game's strcmpi hook.
 *   2. Fallback: scan .text for PUSH references to cannonball-
 *      related strings (CannonBall2 @ 0xA745D8) and look for
 *      decrement instructions (DEC / SUB / MOV byte [addr], 0)
 *      that control ammo count, then NOP them.
 * ================================================================ */

#define CANNONBALL_STR_OFFSET    (CANNONBALL2_STR - 0x400000)      /* "CannonBall2" @ 0xA745D8 */
#define INF_TORPEDOS_STR_OFFSET  (CHEAT_CAPS_INFINITE_TORPEDOS - 0x400000) /* "CHEAT_INFINITE_TORPEDOS" @ 0xBFF49C */

static int g_cannon_enabled = 0;
static int g_native_used    = 0;

/* Fallback patch storage */
#define MAX_CANNON_PATCHES  8
static PatchRecord g_patches[MAX_CANNON_PATCHES];
static int         g_patch_count = 0;
static int         g_fallback_scanned = 0;

/* -----------------------------------------------------------------
 * Helper: NOP a range at the given address.
 * ----------------------------------------------------------------- */
static int add_nop_patch(DWORD addr, int size)
{
    if (g_patch_count >= MAX_CANNON_PATCHES) return 0;
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
 * Fallback: scan .text for cannonball string references and
 * look for decrement instructions (DEC / SUB) to NOP.
 * ----------------------------------------------------------------- */
static int scan_fallback_flags(DWORD str_addr, const char *label)
{
    DWORD text_start, text_size;
    if (!find_text_section(&text_start, &text_size)) {
        LOG("InfiniteCannonballs: failed to find .text section");
        return 0;
    }

    unsigned char *code = (unsigned char *)text_start;
    int found = 0;

    for (DWORD i = 0; i + 8 < text_size && g_patch_count < MAX_CANNON_PATCHES; i++) {
        if (code[i] != 0x68) continue;  /* not PUSH */

        DWORD pushed = *(DWORD *)&code[i + 1];
        if (pushed != str_addr) continue;

        DWORD code_addr = text_start + i;
        LOG("InfiniteCannonballs: found PUSH of %s (0x%08X) at 0x%08X", label, str_addr, code_addr);

        /* Scan forward up to 120 bytes for decrement instructions */
        DWORD search_end = i + 120;
        if (search_end > text_size) search_end = text_size;

        for (DWORD j = i; j + 2 < search_end; j++) {
            /* Pattern 1: FF 8E xx xx xx xx — DEC dword [esi+offset] */
            if (code[j] == 0xFF && code[j + 1] == 0x8E) {
                DWORD dec_addr = text_start + j;
                if (add_nop_patch(dec_addr, 6)) {
                    LOG("InfiniteCannonballs: NOP'd DEC dword at 0x%08X (ammo decrement)", dec_addr);
                    found++;
                    j += 6;
                    continue;
                }
            }

            /* Pattern 2: FF 88 xx xx xx xx — DEC dword [eax+offset] */
            if (code[j] == 0xFF && code[j + 1] == 0x88) {
                DWORD dec_addr = text_start + j;
                if (add_nop_patch(dec_addr, 6)) {
                    LOG("InfiniteCannonballs: NOP'd DEC dword at 0x%08X (ammo decrement)", dec_addr);
                    found++;
                    j += 6;
                    continue;
                }
            }

            /* Pattern 3: 29 xx — SUB rm, r (generic subtraction) */
            if (code[j] == 0x29 && (code[j + 1] & 0xC0) == 0x80) {
                int sub_size = 2;
                DWORD sub_addr = text_start + j;
                if (add_nop_patch(sub_addr, sub_size)) {
                    LOG("InfiniteCannonballs: NOP'd SUB at 0x%08X (potential ammo subtraction)", sub_addr);
                    found++;
                    j += sub_size;
                }
            }
        }
    }

    return found;
}

/* -----------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------- */

void infinite_cannonballs_apply(void)
{
    if (g_cannon_enabled) return;

    DWORD base = (DWORD)GetModuleHandleA(NULL);

    /* Primary: native cheat */
    if (native_cheat_set("CHEAT_INFINITE_TORPEDOS", 1)) {
        g_native_used = 1;
        g_cannon_enabled = 1;
        LOG("InfiniteCannonballs: activated via native cheat (CHEAT_INFINITE_TORPEDOS)");
        return;
    }

    LOG("InfiniteCannonballs: native cheat unavailable, trying fallback AOB");

    /* Fallback: AOB scan */
    if (!g_fallback_scanned && base) {
        /* Try both cannonball strings */
        DWORD str1 = base + CANNONBALL_STR_OFFSET;
        DWORD str2 = base + INF_TORPEDOS_STR_OFFSET;

        scan_fallback_flags(str1, "CannonBall2");
        scan_fallback_flags(str2, "CHEAT_INFINITE_TORPEDOS");
        g_fallback_scanned = 1;
    }

    /* Fallback 2: scan for MOV byte [reg+offset], 0 patterns
     * that look like ammo count resets (88 86 xx xx xx xx) */
    if (g_patch_count == 0 && base) {
        DWORD text_start, text_size;
        if (find_text_section(&text_start, &text_size)) {
            unsigned char *code = (unsigned char *)text_start;
            int fallback2 = 0;

            for (DWORD i = 0; i + 7 < text_size && g_patch_count < MAX_CANNON_PATCHES && !fallback2; i++) {
                /* Pattern: C6 86 xx xx xx xx 00 — MOV byte [esi+offset], 0
                 * (ammo counter reset / decrement) */
                if (code[i] == 0xC6 && code[i + 1] == 0x86 &&
                    code[i + 6] == 0x00) {
                    DWORD flag_addr = text_start + i;
                    if (add_nop_patch(flag_addr, 7)) {
                        LOG("InfiniteCannonballs: NOP'd counter reset at 0x%08X (fallback-2)", flag_addr);
                        g_patch_count++;
                        fallback2++;
                    }
                }

                /* Pattern: 88 86 xx xx xx xx — MOV [esi+offset], al (ammo set) */
                if (code[i] == 0x88 && code[i + 1] == 0x86) {
                    DWORD mov_addr = text_start + i;
                    if (add_nop_patch(mov_addr, 6)) {
                        LOG("InfiniteCannonballs: NOP'd MOV [esi+] at 0x%08X (fallback-2)", mov_addr);
                        g_patch_count++;
                        fallback2++;
                    }
                }
            }
        }
    }

    g_cannon_enabled = 1;
    LOG("InfiniteCannonballs: activated with %d fallback patch(es)", g_patch_count);
}

void infinite_cannonballs_remove(void)
{
    if (!g_cannon_enabled) return;

    if (g_native_used) {
        native_cheat_set("CHEAT_INFINITE_TORPEDOS", 0);
        g_native_used = 0;
    }

    /* Restore fallback patches */
    for (int i = g_patch_count - 1; i >= 0; i--) {
        patch_restore(&g_patches[i]);
    }
    g_patch_count = 0;
    g_cannon_enabled = 0;

    LOG("InfiniteCannonballs: deactivated");
}

int infinite_cannonballs_get_enabled(void)
{
    return g_cannon_enabled;
}
