#include "native_cheats.h"
#include "utils.h"
#include "config.h"
#include <MinHook.h>

/* ================================================================== *
 *  Internal state                                                     *
 * ================================================================== */

/* Module base — resolved once in native_cheats_init() */
static DWORD g_base = 0;

/* ------------------------------------------------------------------ */
/*  strcmpi hook (sub_473770, base+0x73770)                            *
 *                                                                     *
 *  The game uses this function for case-insensitive string comparison *
 *  throughout its cheat system.  We hook it via MinHook so that when  *
 *  a cheat we want to force-active is compared, we return 0 (equal).  *
 * ------------------------------------------------------------------ */
typedef int (__cdecl *strcmpi_t)(const char *a, const char *b);
static strcmpi_t g_orig_strcmpi = NULL;
static int       g_strcmpi_hooked = 0;

/* ------------------------------------------------------------------ */
/*  Force-active table                                                 *
 *                                                                     *
 *  Entries are added by native_cheat_set(..., 1) and removed by       *
 *  native_cheat_set(..., 0).  The hook scans this table and returns   *
 *  0 (equal) whenever either argument matches a force-active entry.   *
 * ------------------------------------------------------------------ */
#define MAX_FORCE_CHEATS 32

static const char *g_force_names[MAX_FORCE_CHEATS];
static int         g_force_active[MAX_FORCE_CHEATS]; /* 0/1 */
static int         g_force_count = 0;

/* ------------------------------------------------------------------ */
/*  Flag address cache                                                 *
 *                                                                     *
 *  When we discover (via AOB scan or static RE) the byte address of   *
 *  a cheat's on/off flag in .data, we store it here so that           *
 *  native_cheat_set(..., 0) can reliably turn it off.                 *
 * ------------------------------------------------------------------ */
#define MAX_CHEAT_FLAGS 48

typedef struct {
    char  name[48];       /* "cheat_xxx" */
    DWORD flag_addr;      /* 0 = unknown */
    int   state;          /* cached last-read value */
} CheatFlag;

static CheatFlag g_cheat_flags[MAX_CHEAT_FLAGS];
static int       g_cheat_flag_count = 0;

/* ------------------------------------------------------------------ */
/*  Helper: register / look up a flag entry                            *
 * ------------------------------------------------------------------ */
static CheatFlag *flag_find(const char *name) {
    for (int i = 0; i < g_cheat_flag_count; i++)
        if (strcmp(g_cheat_flags[i].name, name) == 0)
            return &g_cheat_flags[i];
    return NULL;
}

static CheatFlag *flag_ensure(const char *name) {
    CheatFlag *f = flag_find(name);
    if (f) return f;
    if (g_cheat_flag_count >= MAX_CHEAT_FLAGS) return NULL;
    f = &g_cheat_flags[g_cheat_flag_count++];
    strncpy(f->name, name, sizeof(f->name) - 1);
    f->name[sizeof(f->name) - 1] = '\0';
    f->flag_addr = 0;
    f->state = 0;
    return f;
}

/* ------------------------------------------------------------------ *
 *  strcmpi hook — the core of the native cheat activation system      *
 * ------------------------------------------------------------------ */
static int __cdecl hk_strcmpi(const char *a, const char *b) {
    /* Fast path: no force-active cheats → pass through immediately */
    if (g_force_count == 0)
        return g_orig_strcmpi(a, b);

    /* Check both operands against every force-active cheat name */
    for (int i = 0; i < g_force_count; i++) {
        if (!g_force_active[i] || !g_force_names[i])
            continue;

        /* Case-insensitive check: we use the original strcmpi so that
         * the comparison is guaranteed consistent with the game's own
         * notion of equality. */
        if (g_orig_strcmpi(a, g_force_names[i]) == 0 ||
            g_orig_strcmpi(b, g_force_names[i]) == 0) {
            return 0;   /* "equal" → game will activate the cheat */
        }
    }

    return g_orig_strcmpi(a, b);
}

/* ------------------------------------------------------------------ *
 *  Initialisation                                                     *
 * ------------------------------------------------------------------ */
void native_cheats_init(void) {
    if (g_strcmpi_hooked) return;

    g_base = (DWORD)GetModuleHandleA(NULL);
    if (!g_base) {
        LOG("NativeCheats: GetModuleHandleA failed");
        return;
    }

    /* Install MinHook on the game's strcmpi at sub_473770 */
    LPVOID target = (LPVOID)(g_base + 0x73770);
    MH_STATUS st = MH_CreateHook(target, (LPVOID)hk_strcmpi,
                                 (LPVOID *)&g_orig_strcmpi);
    if (st != MH_OK) {
        LOG("NativeCheats: MH_CreateHook failed (%d) at 0x%08X", st, target);
        return;
    }

    st = MH_EnableHook(target);
    if (st != MH_OK) {
        LOG("NativeCheats: MH_EnableHook failed (%d)", st);
        return;
    }

    g_strcmpi_hooked = 1;
    LOG("NativeCheats: strcmpi hooked at 0x%08X (sub_473770)", target);

    /* Register cheat names from config.h for future flag discovery */
    #define REG_CHEAT(name, str_addr) \
        do { flag_ensure(name); } while(0)

    REG_CHEAT("cheat_scorex10",           CHEAT_STR_SCOREX10);
    REG_CHEAT("cheat_scorex8",            CHEAT_STR_SCOREX8);
    REG_CHEAT("cheat_scorex6",            CHEAT_STR_SCOREX6);
    REG_CHEAT("cheat_scorex4",            CHEAT_STR_SCOREX4);
    REG_CHEAT("cheat_scorex2",            CHEAT_STR_SCOREX2);
    REG_CHEAT("cheat_invincibility",       CHEAT_STR_INVINCIBILITY);
    REG_CHEAT("cheat_always_score_multiply", CHEAT_STR_ALWAYS_SCORE_MULT);
    REG_CHEAT("cheat_extrahearts",         CHEAT_STR_EXTRAHEARTS);
    REG_CHEAT("cheat_minikit_detector",   CHEAT_STR_MINIKIT_DETECTOR);
    REG_CHEAT("cheat_powerbrick_detector", CHEAT_STR_POWERBRICK_DET);
    REG_CHEAT("cheat_regenerate_hearts",   CHEAT_STR_REGENERATE_HEARTS);
    REG_CHEAT("cheat_breatheunderwater",   CHEAT_STR_BREATHEUNDERWATER);
    REG_CHEAT("cheat_stud_magnet",         CHEAT_STR_STUD_MAGNET);
    REG_CHEAT("cheat_doomedrecovery",      CHEAT_STR_DOOMEDRECOVERY);
    REG_CHEAT("cheat_character_studs",     CHEAT_STR_CHARACTER_STUDS);
    REG_CHEAT("cheat_fastbuild",           CHEAT_STR_FASTBUILD);
    REG_CHEAT("cheat_extratoggle",         CHEAT_STR_EXTRATOGGLE);
    REG_CHEAT("cheat_fastfix",             CHEAT_STR_FASTFIX);
    REG_CHEAT("cheat_fastdig",             CHEAT_STR_FASTDIG);
    REG_CHEAT("cheat_disguises",           CHEAT_STR_DISGUISES);

    #undef REG_CHEAT

    LOG("NativeCheats: %d cheat entries registered for flag discovery",
        g_cheat_flag_count);
}

/* ------------------------------------------------------------------ *
 *  Flag discovery via AOB scanning                                    *
 *                                                                     *
 *  For each registered cheat string, we:                              *
 *   1. Read the pointer from the CHEAT_STRING_TABLE                   *
 *   2. Scan .text for code references (PUSH of that pointer)          *
 *   3. Near the reference, look for conditional jumps (JZ/JNZ) that   *
 *      test the comparison result — the flag is often loaded into a   *
 *      register via MOV [reg+offset], 0/1 nearby.                     *
 *   4. Cache the discovered flag address.                             *
 *                                                                     *
 *  This is best-effort; some flags may not be discovered if the       *
 *  code pattern differs.  Unknown flags still work for activation     *
 *  via the strcmpi hook above.                                        *
 * ------------------------------------------------------------------ */
void native_cheats_discover_flags(void) {
    if (!g_base) {
        g_base = (DWORD)GetModuleHandleA(NULL);
        if (!g_base) return;
    }

    DWORD text_start, text_size;
    if (!find_text_section(&text_start, &text_size)) {
        LOG("NativeCheats: cannot find .text section");
        return;
    }

    unsigned char *code = (unsigned char *)text_start;
    DWORD *table = (DWORD *)(g_base + CHEAT_STRING_TABLE);
    int discovered = 0;

    for (int fi = 0; fi < g_cheat_flag_count; fi++) {
        CheatFlag *cf = &g_cheat_flags[fi];
        if (cf->flag_addr != 0) continue;  /* already known */

        const char *name = cf->name;

        /* Locate the cheat string's address via the pointer table */
        DWORD str_addr = 0;
        for (int ti = 0; ti < 48; ti++) {
            DWORD ptr = table[ti];
            if (ptr >= g_base && ptr < g_base + 0x2000000) {
                if (strcmp((const char *)ptr, name) == 0) {
                    str_addr = (DWORD)ptr;
                    break;
                }
            }
        }
        if (!str_addr) {
            /* Try direct lookup from config.h addresses */
            #define TRY_DIRECT(cheat_name, const_addr) \
                if (strcmp(name, cheat_name) == 0) { \
                    str_addr = const_addr; \
                }
            TRY_DIRECT("cheat_scorex10",           CHEAT_STR_SCOREX10);
            TRY_DIRECT("cheat_scorex8",            CHEAT_STR_SCOREX8);
            TRY_DIRECT("cheat_scorex6",            CHEAT_STR_SCOREX6);
            TRY_DIRECT("cheat_scorex4",            CHEAT_STR_SCOREX4);
            TRY_DIRECT("cheat_scorex2",            CHEAT_STR_SCOREX2);
            TRY_DIRECT("cheat_invincibility",       CHEAT_STR_INVINCIBILITY);
            TRY_DIRECT("cheat_always_score_multiply", CHEAT_STR_ALWAYS_SCORE_MULT);
            TRY_DIRECT("cheat_extrahearts",         CHEAT_STR_EXTRAHEARTS);
            TRY_DIRECT("cheat_minikit_detector",    CHEAT_STR_MINIKIT_DETECTOR);
            TRY_DIRECT("cheat_powerbrick_detector", CHEAT_STR_POWERBRICK_DET);
            TRY_DIRECT("cheat_regenerate_hearts",   CHEAT_STR_REGENERATE_HEARTS);
            TRY_DIRECT("cheat_breatheunderwater",   CHEAT_STR_BREATHEUNDERWATER);
            TRY_DIRECT("cheat_stud_magnet",         CHEAT_STR_STUD_MAGNET);
            TRY_DIRECT("cheat_doomedrecovery",      CHEAT_STR_DOOMEDRECOVERY);
            TRY_DIRECT("cheat_character_studs",     CHEAT_STR_CHARACTER_STUDS);
            TRY_DIRECT("cheat_fastbuild",           CHEAT_STR_FASTBUILD);
            TRY_DIRECT("cheat_extratoggle",         CHEAT_STR_EXTRATOGGLE);
            TRY_DIRECT("cheat_fastfix",             CHEAT_STR_FASTFIX);
            TRY_DIRECT("cheat_fastdig",             CHEAT_STR_FASTDIG);
            TRY_DIRECT("cheat_disguises",           CHEAT_STR_DISGUISES);
            #undef TRY_DIRECT
        }
        if (!str_addr) continue;

        /* Scan .text for a PUSH of an address close to str_addr
         * (the actual string address may be slightly different from
         *  the pointer-table value due to indirection). */
        for (DWORD si = 0; si + 8 < text_size; si++) {
            /* Pattern: 68 xx xx xx xx  = PUSH imm32 */
            if (code[si] == 0x68) {
                DWORD pushed = *(DWORD *)(code + si + 1);
                if (pushed != str_addr &&
                    pushed != (DWORD)name) continue;

                DWORD code_addr = text_start + si;

                /* Look for nearby flag bytes: scan forward up to 80
                 * bytes from the code reference.  We look for a write
                 * like MOV [addr], 0/1 or CMP [addr], 0/1. */
                DWORD search_end = si + 80;
                if (search_end > text_size) search_end = text_size;

                for (DWORD dj = si; dj < search_end; dj++) {
                    /* Pattern: C6 05 xx xx xx xx 00/01
                     *          MOV byte [addr], 0/1 */
                    if (code[dj] == 0xC6 && code[dj+1] == 0x05) {
                        DWORD flag_addr = *(DWORD *)(code + dj + 2);
                        int   flag_val = code[dj + 6];

                        if ((flag_val == 0 || flag_val == 1) &&
                            flag_addr >= g_base &&
                            flag_addr < g_base + 0x2000000) {

                            /* Validate: read current value */
                            int cur = *(volatile int *)flag_addr;
                            if (cur == 0 || cur == 1) {
                                cf->flag_addr = flag_addr;
                                cf->state = cur;
                                discovered++;
                                LOG("NativeCheats: %s flag at 0x%08X (val=%d)",
                                    name, flag_addr, cur);
                                goto next_cheat;
                            }
                        }
                    }

                    /* Pattern: 88 15 xx xx xx xx  = MOV [addr], dl
                     * followed later by a CMP [addr], 0/1 reference */
                    if (code[dj] == 0x88 && code[dj+1] == 0x15) {
                        DWORD flag_addr = *(DWORD *)(code + dj + 2);
                        if (flag_addr >= g_base &&
                            flag_addr < g_base + 0x2000000) {
                            /* Probable flag — store and verify later */
                            cf->flag_addr = flag_addr;
                            cf->state = *(volatile int *)flag_addr;
                            discovered++;
                            LOG("NativeCheats: %s flag (MOV [addr]) at 0x%08X (val=%d)",
                                name, flag_addr, cf->state);
                            goto next_cheat;
                        }
                    }
                }

                /* Log reference for manual RE even if flag not found */
                LOG("NativeCheats: %s code ref at 0x%08X (no flag byte found)",
                    name, code_addr);
            }
        }
next_cheat:;
    }

    LOG("NativeCheats: flag discovery complete — %d new flag(s) found",
        discovered);
}

/* ------------------------------------------------------------------ *
 *  Public API                                                         *
 * ------------------------------------------------------------------ */

int native_cheat_set(const char *cheat_name, int enable) {
    if (!cheat_name) return 0;

    /* Ensure initialised (idempotent) */
    if (!g_strcmpi_hooked)
        native_cheats_init();

    if (!g_strcmpi_hooked) {
        LOG("NativeCheats: cannot activate \"%s\" — strcmpi hook not installed",
            cheat_name);
        return 0;
    }

    /* Ensure a flag entry exists for this cheat */
    CheatFlag *cf = flag_ensure(cheat_name);
    if (!cf) return 0;

    if (enable) {
        /* Add to force-active table if not already present */
        int found = 0;
        for (int i = 0; i < g_force_count; i++) {
            if (g_orig_strcmpi(g_force_names[i], cheat_name) == 0) {
                g_force_active[i] = 1;
                found = 1;
                break;
            }
        }
        if (!found && g_force_count < MAX_FORCE_CHEATS) {
            g_force_names[g_force_count] = cf->name;
            g_force_active[g_force_count] = 1;
            g_force_count++;
        }

        /* If we also know the flag address, force it now */
        if (cf->flag_addr) {
            BYTE v_on = 1;
            safe_write(cf->flag_addr, &v_on, 1);
        }

        LOG("NativeCheats: %s -> ON (force-active, pending game match)", cheat_name);
    } else {
        /* Remove from force-active table */
        for (int i = 0; i < g_force_count; i++) {
            if (g_orig_strcmpi(g_force_names[i], cheat_name) == 0) {
                g_force_active[i] = 0;
                break;
            }
        }

        /* If we know the flag address, write 0 directly */
        if (cf->flag_addr) {
            BYTE v_off = 0;
            safe_write(cf->flag_addr, &v_off, 1);
            cf->state = 0;
            LOG("NativeCheats: %s -> OFF (flag at 0x%08X cleared)", cheat_name, cf->flag_addr);
        } else {
            LOG("NativeCheats: %s -> OFF (force-removed, flag addr unknown — "
                "run discover_flags for direct toggle)", cheat_name);
        }
    }

    return 1;
}

int native_cheat_get(const char *cheat_name) {
    if (!cheat_name) return 0;

    CheatFlag *cf = flag_find(cheat_name);
    if (!cf) return 0;

    /* If we know the flag address, read the current value */
    if (cf->flag_addr) {
        int val = *(volatile int *)cf->flag_addr;
        cf->state = (val != 0) ? 1 : 0;
        return cf->state;
    }

    /* Otherwise check the force-active table */
    for (int i = 0; i < g_force_count; i++) {
        if (g_orig_strcmpi && g_orig_strcmpi(g_force_names[i], cheat_name) == 0)
            return g_force_active[i];
    }

    return 0;
}

/* ------------------------------------------------------------------ *
 *  Convenience wrappers                                               *
 * ------------------------------------------------------------------ */

void native_stud_magnet(int enable) {
    native_cheat_set("cheat_stud_magnet", enable);
}

void native_score_multiplier(int multiplier) {
    /* Map multiplier value to cheat name */
    const char *cheats[] = {
        NULL, NULL,          /*  0,  1 */
        "cheat_scorex2",     /*  2 */
        "cheat_scorex3",     /*  3 */
        "cheat_scorex4",     /*  4 */
        NULL,                /*  5 */
        "cheat_scorex6",     /*  6 */
        NULL,                /*  7 */
        "cheat_scorex8",     /*  8 */
        NULL,                /*  9 */
        "cheat_scorex10"     /* 10 */
    };

    if (multiplier <= 0) {
        /* Disable all score multipliers */
        native_cheat_set("cheat_always_score_multiply", 0);
        for (int m = 2; m <= 10; m += 2) {
            if (cheats[m])
                native_cheat_set(cheats[m], 0);
        }
    } else if (multiplier >= 2 && multiplier <= 10 && cheats[multiplier]) {
        native_cheat_set(cheats[multiplier], 1);
    }
}

void native_always_score_multiply(int enable) {
    native_cheat_set("cheat_always_score_multiply", enable);
}

void native_invincibility(int enable) {
    native_cheat_set("cheat_invincibility", enable);
}

void native_extra_hearts(int enable) {
    native_cheat_set("cheat_extrahearts", enable);
}

void native_breathe_underwater(int enable) {
    native_cheat_set("cheat_breatheunderwater", enable);
}

void native_fast_build(int enable) {
    native_cheat_set("cheat_fastbuild", enable);
}

void native_fast_fix(int enable) {
    native_cheat_set("cheat_fastfix", enable);
}

void native_fast_dig(int enable) {
    native_cheat_set("cheat_fastdig", enable);
}

void native_regenerate_hearts(int enable) {
    native_cheat_set("cheat_regenerate_hearts", enable);
}

void native_disguises(int enable) {
    native_cheat_set("cheat_disguises", enable);
}

void native_character_studs(int enable) {
    native_cheat_set("cheat_character_studs", enable);
}

void native_minikit_detector(int enable) {
    native_cheat_set("cheat_minikit_detector", enable);
}

void native_powerbrick_detector(int enable) {
    native_cheat_set("cheat_powerbrick_detector", enable);
}

void native_doomed_recovery(int enable) {
    native_cheat_set("cheat_doomedrecovery", enable);
}

void native_extra_toggle(int enable) {
    native_cheat_set("cheat_extratoggle", enable);
}
