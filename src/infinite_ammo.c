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
    /* Tier 2 disabled: generic DEC/SUB patterns match ~128 unrelated sites
     * in .text with no link to ammo logic — NOP'ing them is destructive.
     * Tier 1 (native cheat) and Tier 3 (force-write) are unaffected.
     * (Subsumes decoder fixes: the (modrm>>3)&3 mask and SIB+disp8 length
     * bugs lived in the removed scan loops.) */
    LOG("Infinite Ammo: Tier 2 disabled — pattern too generic, skipping AOB scan");
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
