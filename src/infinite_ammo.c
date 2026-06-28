#include "infinite_ammo.h"
#include "utils.h"
#include "config.h"
#include "native_cheats.h"

/* ================================================================
 * Infinite Ammo Module
 *
 * Implementation:
 *   Tier 1: Activate the game's built-in CHEAT_INFINITE_TORPEDOS
 *           via the native cheat strcmpi hook.
 *   Tier 2: AOB-scan the .text section for common ammo decrement
 *           patterns and NOP them using PatchRecord.
 *   Tier 3: Force max_ammo / torpedo_capacity to a high value
 *           each frame as a final fallback.
 *
 * All three tiers are attempted; successful ones persist.
 * ================================================================ */

/* ------------------------------------------------------------------ */
/*  Internal state                                                     */
/* ------------------------------------------------------------------ */

/* Module base resolved once */
static DWORD g_base = 0;

/* --- Tier 2: PatchRecord array for AOB-discovered sites --- */
#define MAX_AMMO_PATCHES 128

static PatchRecord g_patches[MAX_AMMO_PATCHES];
static int         g_patch_count = 0;
static int         g_patches_applied = 0;

/* --- Tier 3: Force-write addresses for continuous override --- */
#define MAX_FORCE_ADDRS 8

static struct {
    DWORD addr;
    DWORD value;
} g_force_addrs[MAX_FORCE_ADDRS];
static int g_force_count = 0;

/* --- Tier tracking for logging --- */
static int g_tier1_active = 0;
static int g_tier2_count  = 0;
static int g_tier3_active = 0;

/* ------------------------------------------------------------------ */
/*  Tier 1: Native cheat activation                                   */
/* ------------------------------------------------------------------ */

/**
 * Attempt to activate the game's built-in CHEAT_INFINITE_TORPEDOS
 * via the strcmpi hook installed by native_cheats_init().
 *
 * The cheat name is "cheat_infinite_torpedos" following the pattern
 * of other native cheats (cheat_stud_magnet, cheat_invincibility, etc.).
 */
static void tier1_try_activate(void)
{
    if (g_tier1_active) return;

    /* Ensure the native cheat subsystem is initialised */
    native_cheats_init();

    /* Attempt to force-enable the cheat via the strcmpi hook.
     * If the name matches the game's internal cheat name, the hook
     * returns 0 and the game activates the cheat. */
    int ok = native_cheat_set("cheat_infinite_torpedos", 1);
    if (ok) {
        LOG("Infinite Ammo: Tier 1 (native cheat) activated \"cheat_infinite_torpedos\"");
        g_tier1_active = 1;
    } else {
        LOG("Infinite Ammo: Tier 1 (native cheat) failed — moving to Tier 2");
    }
}

static void tier1_deactivate(void)
{
    if (!g_tier1_active) return;
    native_cheat_set("cheat_infinite_torpedos", 0);
    g_tier1_active = 0;
    LOG("Infinite Ammo: Tier 1 (native cheat) deactivated");
}

/* ------------------------------------------------------------------ */
/*  Tier 2: AOB scan + NOP via PatchRecord                            */
/* ------------------------------------------------------------------ */

/**
 * @brief Work out the full length of a ModR/M-addressed instruction.
 *
 * Given the opcode byte and the ModR/M byte that follows, determine
 * the total instruction size.  This is a simplified decoder for the
 * patterns we care about (FF /0 INC, FF /1 DEC, etc.).
 *
 * @param opcode  The first opcode byte.
 * @param modrm   The ModR/M byte.
 * @return Total instruction length (2-7 bytes), or 0 if invalid.
 */
static int modrm_insn_len(unsigned char opcode, unsigned char modrm)
{
    unsigned char mod = (modrm >> 6) & 3;
    unsigned char rm  = modrm & 7;

    BPE_UNUSED(opcode);

    /* Base: opcode + ModR/M = 2 bytes */
    int len = 2;

    if (mod == 1) {
        /* disp8 */
        len += 1;
    } else if (mod == 2) {
        /* disp32 */
        len += 4;
    } else if (mod == 0) {
        if (rm == 4) {
            /* [--][--] with SIB → disp8 SIB or disp32 SIB is still valid
             * but for mod=0, [SIB] = SIB only, no displacement */
            len += 1; /* SIB */
        } else if (rm == 5) {
            /* [disp32] */
            len += 4;
        }
        /* else: [reg] — no extra bytes */
    }

    /* SIB (r/m == 4 always means SIB follows in memory-addressing mode) */
    if (rm == 4 && mod != 3) {
        len += 1; /* SIB byte */
        /* If mod == 1 there is also a disp8 after SIB, already counted above */
        /* If mod == 2 there is also a disp32 after SIB, already counted above */
    }

    return len;
}

/**
 * @brief Scan .text for ammo decrement patterns and register PatchRecords.
 *
 * Patterns scanned:
 *   - FF 8? .. .. .. ..   DEC dword ptr [reg+disp32] (reg=001 in ModR/M)
 *   - FF 0?                DEC dword ptr [reg]        (reg=001, mod=00)
 *   - 29 0? .. .. .. ..   SUB [disp32], ecx
 *   - 29 1? .. .. .. ..   SUB [disp32], edx
 *   - 83 2? .. ..          SUB dword ptr [reg], byte_imm
 *   - 83 6? .. ..          SUB dword ptr [addr], byte_imm
 *
 * Each match registers a PatchRecord with space for NOP bytes.
 * Does nothing if patches have already been registered.
 */
static void tier2_scan_patches(void)
{
    if (g_patch_count > 0) return;

    DWORD text_start, text_size;
    if (!find_text_section(&text_start, &text_size)) {
        LOG("Infinite Ammo: Tier 2 — cannot find .text section");
        return;
    }

    unsigned char *code = (unsigned char *)text_start;

    /* ------ Pattern: FF /1 (DEC r/m32) ------ */
    for (DWORD i = 0; i + 2 < text_size && g_patch_count < MAX_AMMO_PATCHES; i++) {
        if (code[i] != 0xFF) continue;

        unsigned char modrm = code[i + 1];
        unsigned char reg   = (modrm >> 3) & 7;

        if (reg != 1) continue; /* we only want /1 = DEC */

        unsigned char mod = (modrm >> 6) & 3;
        unsigned char rm  = modrm & 7;

        int ins_len = 2; /* FF + ModR/M */

        /* Determine instruction length from addressing mode */
        if (mod == 3) {
            /* Register form: DEC reg — skip, we only patch memory ops */
            continue;
        } else if (mod == 0) {
            if (rm == 4) { ins_len = 3; }      /* [SIB] — add SIB byte */
            else if (rm == 5) { ins_len = 6; } /* [disp32] */
            else { ins_len = 2; }              /* [reg] */
        } else if (mod == 1) {
            if (rm == 4) { ins_len = 3; }
            else { ins_len = 3; }             /* [reg+disp8] */
        } else if (mod == 2) {
            if (rm == 4) { ins_len = 7; }      /* [SIB+disp32] */
            else { ins_len = 6; }              /* [reg+disp32] */
        }

        /* Handle SIB */
        if (rm == 4 && mod != 3) {
            /* SIB byte at i+2, read scale/index/base if needed for length */
            /* Length already set correctly above */
        }

        if (i + ins_len > text_size) continue;

        g_patches[g_patch_count].addr  = text_start + i;
        g_patches[g_patch_count].size  = ins_len;
        g_patches[g_patch_count].orig_bytes = NULL;
        g_patches[g_patch_count].active = 0;
        g_patch_count++;
    }

    /* ------ Pattern: SUB [disp32], reg (29 0x/1x .. .. .. ..) ------ */
    for (DWORD i = 0; i + 6 < text_size && g_patch_count < MAX_AMMO_PATCHES; i++) {
        if (code[i] != 0x29) continue;
        unsigned char modrm = code[i + 1];
        unsigned char mod   = (modrm >> 6) & 3;
        unsigned char reg   = (modrm >> 3) & 3; /* ECX or EDX */
        unsigned char rm    = modrm & 7;

        /* We're looking for SUB [addr], ecx/edx — immediate destination */
        if (mod != 0 || rm != 5) continue; /* must be [disp32] */
        if (reg > 1) continue; /* ECX=1, EDX=2 — other regs less relevant for ammo */

        /* Size: 29 + ModR/M + disp32 = 6 bytes */
        g_patches[g_patch_count].addr  = text_start + i;
        g_patches[g_patch_count].size  = 6;
        g_patches[g_patch_count].orig_bytes = NULL;
        g_patches[g_patch_count].active = 0;
        g_patch_count++;
    }

    /* ------ Pattern: SUB dword ptr [addr], imm8 (83 2x .. .. .. .. ..) ------ */
    for (DWORD i = 0; i + 3 < text_size && g_patch_count < MAX_AMMO_PATCHES; i++) {
        if (code[i] != 0x83) continue;
        unsigned char modrm = code[i + 1];
        unsigned char reg   = (modrm >> 3) & 7;

        /* /5 = SUB; /0 = ADD — try /1 (OR), /5 (SUB) */
        if (reg != 5) continue; /* SUB */

        unsigned char mod = (modrm >> 6) & 3;
        unsigned char rm  = modrm & 7;

        int ins_len = 3; /* 83 + ModR/M + imm8 */

        if (mod == 1) {
            ins_len = 4; /* + disp8 */
        } else if (mod == 2) {
            ins_len = 7; /* + disp32 */
        } else if (mod == 0) {
            if (rm == 4)      ins_len = 4;  /* SIB */
            else if (rm == 5) ins_len = 7;  /* [disp32] */
        }

        /* Handle SIB */
        if (rm == 4 && mod != 3) {
            if (mod == 1) ins_len = 5;  /* SIB + disp8 + imm8 */
            else if (mod == 2) ins_len = 8;  /* SIB + disp32 + imm8 */
            else ins_len = 4;  /* SIB + imm8 */
        }

        if (i + ins_len > text_size) continue;

        g_patches[g_patch_count].addr  = text_start + i;
        g_patches[g_patch_count].size  = ins_len;
        g_patches[g_patch_count].orig_bytes = NULL;
        g_patches[g_patch_count].active = 0;
        g_patch_count++;
    }

    g_tier2_count = g_patch_count;

    if (g_tier2_count > 0) {
        LOG("Infinite Ammo: Tier 2 (AOB) found %d patch candidates", g_tier2_count);
    } else {
        LOG("Infinite Ammo: Tier 2 (AOB) found no patch candidates");
    }
}

static void tier2_apply(void)
{
    if (g_patches_applied) return;
    if (g_patch_count == 0) return;

    int success = 0;
    for (int i = 0; i < g_patch_count; i++) {
        /* Create a NOP buffer of the right size */
        unsigned char *nops = (unsigned char *)malloc(g_patches[i].size);
        if (!nops) continue;
        memset(nops, 0x90, g_patches[i].size);

        if (patch_apply(&g_patches[i], nops)) {
            success++;
        }
        free(nops);
    }

    g_patches_applied = 1;
    g_tier2_count = success;
    LOG("Infinite Ammo: Tier 2 applied %d NOP patches", success);
}

static void tier2_remove(void)
{
    if (!g_patches_applied) return;

    int restored = 0;
    for (int i = 0; i < g_patch_count; i++) {
        if (g_patches[i].active) {
            if (patch_restore(&g_patches[i])) {
                restored++;
            }
        }
    }

    g_patches_applied = 0;
    LOG("Infinite Ammo: Tier 2 restored %d patch(es)", restored);
}

/* ------------------------------------------------------------------ */
/*  Tier 3: Force-write max_ammo each frame                            */
/* ------------------------------------------------------------------ */

/**
 * Register known ammo-related addresses for continuous override.
 *
 * These are derived from static RE of LEGO Pirates:
 *   - max_ammo        at 0xBEDC7C
 *   - torpedo_infinite at 0xBF5138
 *   - torpedo_capacity at 0xBF514C
 */
static void tier3_register_force_addrs(void)
{
    if (g_force_count > 0) return;

    /* max_ammo — set to a large value (999) */
    if (g_force_count < MAX_FORCE_ADDRS) {
        g_force_addrs[g_force_count].addr  = 0xBEDC7C;
        g_force_addrs[g_force_count].value = 999;
        g_force_count++;
    }

    /* torpedo_capacity — set to max */
    if (g_force_count < MAX_FORCE_ADDRS) {
        g_force_addrs[g_force_count].addr  = 0xBF514C;
        g_force_addrs[g_force_count].value = 999;
        g_force_count++;
    }

    /* torpedo_infinite flag — set to 1 */
    if (g_force_count < MAX_FORCE_ADDRS) {
        g_force_addrs[g_force_count].addr  = 0xBF5138;
        g_force_addrs[g_force_count].value = 1;
        g_force_count++;
    }

    g_tier3_active = 1;

    if (g_force_count > 0) {
        LOG("Infinite Ammo: Tier 3 registered %d force-write address(es)", g_force_count);
    }
}

static void tier3_force_values(void)
{
    if (!g_tier3_active) return;

    for (int i = 0; i < g_force_count; i++) {
        DWORD old;
        if (VirtualProtect((LPVOID)g_force_addrs[i].addr, 4, PAGE_READWRITE, &old)) {
            *(DWORD *)g_force_addrs[i].addr = g_force_addrs[i].value;
            VirtualProtect((LPVOID)g_force_addrs[i].addr, 4, old, &old);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

void infinite_ammo_apply(void)
{
    /* Resolve module base once */
    if (!g_base) {
        g_base = (DWORD)GetModuleHandleA(NULL);
    }

    /* Tier 1: Try native cheat activation */
    tier1_try_activate();

    /* Tier 2: Scan and apply AOB patches (only once) */
    tier2_scan_patches();
    tier2_apply();

    /* Tier 3: Register and force-write ammo values each frame */
    tier3_register_force_addrs();
    tier3_force_values();
}

void infinite_ammo_remove(void)
{
    /* Tier 1: Deactivate native cheat */
    tier1_deactivate();

    /* Tier 2: Restore all AOB patches */
    tier2_remove();

    /* Tier 3: Nothing to undo — values naturally return to normal
     *          once we stop overwriting them.  Keep registered. */

    LOG("Infinite Ammo: removed");
}
