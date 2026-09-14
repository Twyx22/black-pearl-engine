#include "noclip.h"
#include "utils.h"
#include "config.h"
#include <string.h>

/* ================================================================
 * NoClip — Disable player collision detection
 *
 * Dual approach:
 *   1. Primary: Find CALL instructions to the collision handler
 *      (sub_6AC280 @ base+0x2AC280, 6226 bytes) and NOP them.
 *      This is the main collision/area detection function.
 *
 *   2. Secondary: Find PUSH references to collision-disable strings
 *      (no_character_collisions, no_collision, Disable Collision)
 *      and force the comparison branch to always take the "disable"
 *      path by changing JZ -> JMP.
 *
 * All patches use PatchRecord for clean apply/restore.
 * ================================================================ */

/* Offsets from module base (preferred base 0x400000, IDA-verified) */
#define NOCLIP_COLLISION_FUNC_OFFSET  0x2AC280  /* sub_6AC280 */
#define NOCLIP_STR1_OFFSET            0x7E0A38  /* "no_character_collisions" */
#define NOCLIP_STR2_OFFSET            0x7F6820  /* "no_collision" */
#define NOCLIP_STR3_OFFSET            0x7B9EE8  /* "Disable Collision" */

#define MAX_NOCLIP_PATCHES  64
static PatchRecord g_patches[MAX_NOCLIP_PATCHES];
static unsigned char g_patch_kind[MAX_NOCLIP_PATCHES]; /* 0=NOP, 1=JZ->JMP */
static int         g_patch_count = 0;
static int         g_noclip_active = 0;
static int         g_noclip_scanned = 0;

/* -----------------------------------------------------------------
 * Helper: add a NOP patch at the given address (size must be <= 20)
 * Returns 1 on success, 0 on failure.
 * ----------------------------------------------------------------- */
static int add_nop_patch(DWORD addr, int size)
{
    if (g_patch_count >= MAX_NOCLIP_PATCHES)
        return 0;
    if (size <= 0 || size > 20)
        return 0;

    PatchRecord *pr = &g_patches[g_patch_count];
    memset(pr, 0, sizeof(PatchRecord));
    pr->addr = addr;
    pr->size = (size_t)size;

    unsigned char nops[20];
    memset(nops, 0x90, (size_t)size);

    if (patch_apply(pr, nops)) {
        g_patch_kind[g_patch_count] = 0; /* NOP patch */
        g_patch_count++;
        return 1;
    }
    return 0;
}

/* -----------------------------------------------------------------
 * Approach 1: NOP direct CALL instructions to the collision handler.
 *
 * Scan all CALL (0xE8) instructions in .text, compute their target,
 * and if it matches the collision function address, NOP the 5-byte CALL.
 * ----------------------------------------------------------------- */
static void scan_collision_calls(void)
{
    DWORD text_start, text_size;
    if (!find_text_section(&text_start, &text_size)) {
        LOG("NoClip: failed to find .text section");
        return;
    }

    DWORD base = (DWORD)GetModuleHandleA(NULL);
    if (!base) {
        LOG("NoClip: GetModuleHandleA failed");
        return;
    }

    DWORD collision_func = base + NOCLIP_COLLISION_FUNC_OFFSET;
    unsigned char *code = (unsigned char *)text_start;
    int found = 0;

    for (DWORD i = 0; i + 5 < text_size && g_patch_count < MAX_NOCLIP_PATCHES; i++) {
        if (code[i] != 0xE8)
            continue;  /* not a CALL */

        DWORD rel32   = *(DWORD *)&code[i + 1];
        DWORD target  = text_start + i + 5 + rel32;

        if (target != collision_func)
            continue;

        DWORD call_addr = text_start + i;
        if (add_nop_patch(call_addr, 5)) {
            LOG("NoClip: NOP'd collision CALL at 0x%08X", call_addr);
            found++;
        }
    }

    LOG("NoClip: approach-1 found and patched %d CALL(s) to collision func 0x%08X",
        found, collision_func);
}

/* -----------------------------------------------------------------
 * Approach 2: Force collision-disable property checks.
 *
 * Find PUSH instructions that reference the runtime address of any
 * known collision-disable string.  For each:
 *   1. Look for a CALL (0xE8) within the next 15 bytes.
 *   2. After the CALL, look for TEST+JZ pattern (84 C0 / 85 C0 + 74 xx).
 *   3. Change the JZ (0x74) to JMP (0xEB) so the disable path is
 *      always taken.
 *
 * This is more targeted than NOPing the CALL — it preserves the
 * string comparison function's existence but makes the result
 * always "collision disabled".
 * ----------------------------------------------------------------- */
static DWORD g_string_offsets[3] = {
    NOCLIP_STR1_OFFSET,  /* "no_character_collisions" */
    NOCLIP_STR2_OFFSET,  /* "no_collision" */
    NOCLIP_STR3_OFFSET,  /* "Disable Collision" */
};

static void scan_string_references(void)
{
    DWORD text_start, text_size;
    if (!find_text_section(&text_start, &text_size))
        return;

    DWORD base = (DWORD)GetModuleHandleA(NULL);
    if (!base)
        return;

    /* Compute runtime addresses for all three strings */
    DWORD str_runtime[3];
    for (int s = 0; s < 3; s++)
        str_runtime[s] = base + g_string_offsets[s];

    unsigned char *code = (unsigned char *)text_start;
    int found = 0;

    for (DWORD i = 0; i + 5 < text_size; i++) {
        if (code[i] != 0x68)
            continue;  /* not a PUSH dword */

        DWORD push_val = *(DWORD *)&code[i + 1];

        /* Check if PUSH value matches any of our string addresses */
        int matched_str = -1;
        for (int s = 0; s < 3; s++) {
            if (push_val == str_runtime[s]) {
                matched_str = s;
                break;
            }
        }
        if (matched_str < 0)
            continue;

        /* Found a PUSH of a collision-disable string.
         * Look for a nearby CALL instruction within the next 15 bytes. */
        for (int j = 5; j < 15 && i + j + 5 < text_size; j++) {
            if (code[i + j] != 0xE8)
                continue;

            DWORD call_addr = text_start + i + j;

            /* Skip if we already patched this address */
            int already = 0;
            for (int k = 0; k < g_patch_count; k++) {
                if (g_patches[k].addr == call_addr) {
                    already = 1;
                    break;
                }
            }
            if (already)
                break;

            /* Look for TEST+JZ pattern after the CALL.
             * Common patterns:
             *   84 C0 = TEST AL, AL  (2 bytes)
             *   85 C0 = TEST EAX, EAX (2 bytes)
             *   74 xx = JZ rel8 (2 bytes) */
            DWORD after_call = i + j + 5;  /* byte after the CALL */
            int max_look = 12;             /* look within 12 bytes after CALL */

            for (int k = 0; k + 4 <= max_look && after_call + k + 4 < text_size; k++) {
                /* Check for TEST AL/JZ or TEST EAX/JZ */
                int is_test_jz = 0;
                DWORD jz_addr = 0;

                if (code[after_call + k] == 0x84 && code[after_call + k + 1] == 0xC0) {
                    /* TEST AL, AL (2 bytes) */
                    if (after_call + k + 2 + 2 < text_size &&
                        code[after_call + k + 2] == 0x74) {
                        /* JZ found! */
                        jz_addr = text_start + after_call + k + 2;
                        is_test_jz = 1;
                    }
                } else if (code[after_call + k] == 0x85 && code[after_call + k + 1] == 0xC0) {
                    /* TEST EAX, EAX (2 bytes) */
                    if (after_call + k + 2 + 2 < text_size &&
                        code[after_call + k + 2] == 0x74) {
                        jz_addr = text_start + after_call + k + 2;
                        is_test_jz = 1;
                    }
                }

                if (!is_test_jz)
                    continue;

                /* Found JZ after TEST. Change 0x74 -> 0xEB (JMP).
                 * The relative offset stays the same. */
                unsigned char jmp_byte = 0xEB;

                if (g_patch_count >= MAX_NOCLIP_PATCHES) {
                    LOG("NoClip: patch table full, skipping JZ->JMP at 0x%08X", jz_addr);
                    break;
                }
                PatchRecord *pr = &g_patches[g_patch_count];
                memset(pr, 0, sizeof(PatchRecord));
                pr->addr = jz_addr;
                pr->size = 1;

                if (patch_apply(pr, &jmp_byte)) {
                    g_patch_kind[g_patch_count] = 1; /* JZ->JMP patch */
                    g_patch_count++;
                    found++;
                    LOG("NoClip: JZ->JMP at 0x%08X (string ref 0x%08X)",
                        jz_addr, call_addr);
                }

                break;  /* stop looking for this CALL */
            }

            break;  /* only handle the first CALL after each PUSH */
        }
    }

    if (found > 0) {
        LOG("NoClip: approach-2 flipped %d JZ->JMP for collision strings", found);
    }
}

/* -----------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------- */

void noclip_apply(void)
{
    if (g_noclip_active)
        return;

    LOG("NoClip: applying patches...");

    /* First call: scan and patch */
    if (!g_noclip_scanned) {
        g_noclip_scanned = 1;
        scan_collision_calls();
        scan_string_references();
    } else {
        /* Re-apply after noclip_remove (records inactive): rewrite the
         * original patch bytes per kind — NOP patches get NOPs,
         * JZ->JMP patches get 0xEB (a NOP here would corrupt the branch). */
        for (int i = 0; i < g_patch_count; i++) {
            if (g_patch_kind[i] == 1) {
                unsigned char jmp = 0xEB;
                patch_apply(&g_patches[i], &jmp);
            } else {
                unsigned char nops[20];
                memset(nops, 0x90, g_patches[i].size);
                patch_apply(&g_patches[i], nops);
            }
        }
    }

    g_noclip_active = 1;
    LOG("NoClip: active (%d patch site(s))", g_patch_count);
}

void noclip_remove(void)
{
    if (!g_noclip_active)
        return;

    /* Restore in reverse order */
    for (int i = g_patch_count - 1; i >= 0; i--)
        patch_restore(&g_patches[i]);

    g_noclip_active = 0;
    LOG("NoClip: disabled (restored %d patch site(s))", g_patch_count);
}
