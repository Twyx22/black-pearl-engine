#include "cheats.h"
#include "utils.h"
#include "hooks.h"
#include "config.h"

CheatsState g_cheats = {0, 0, 0, 0, 100, 1, 0, 0, 0, 0, 0, 1, CUSTOM_STUD_DEFAULT, 1, GOLDEN_BRICK_DEFAULT, 0, 0, 0, 100};

/* Stud patch: NOP sub ebx,eax and sbb esi,edx at fixed addresses (Cheat Engine found) */
static unsigned char g_stud_sub_orig[2] = {0};
static unsigned char g_stud_sbb_orig[2] = {0};
static int g_stud_patched = 0;

void apply_stud_patch(void) {
    if (g_stud_patched) return;
    
    /* Read original bytes */
    memcpy(g_stud_sub_orig, (void*)STUD_SUB_ADDR, 2);
    memcpy(g_stud_sbb_orig, (void*)STUD_SBB_ADDR, 2);
    
    /* NOP sub ebx,eax (2 bytes) */
    unsigned char nop2[2] = {0x90, 0x90};
    DWORD old;
    if (VirtualProtect((LPVOID)STUD_SUB_ADDR, 2, PAGE_EXECUTE_READWRITE, &old)) {
        memcpy((void*)STUD_SUB_ADDR, nop2, 2);
        VirtualProtect((LPVOID)STUD_SUB_ADDR, 2, old, &old);
        LOG("Infinite Studs: NOP'd sub ebx,eax at 0x%08X", STUD_SUB_ADDR);
    } else {
        LOG("Infinite Studs: VirtualProtect FAILED at 0x%08X", STUD_SUB_ADDR);
        return;
    }
    
    /* NOP sbb esi,edx (2 bytes) */
    if (VirtualProtect((LPVOID)STUD_SBB_ADDR, 2, PAGE_EXECUTE_READWRITE, &old)) {
        memcpy((void*)STUD_SBB_ADDR, nop2, 2);
        VirtualProtect((LPVOID)STUD_SBB_ADDR, 2, old, &old);
        LOG("Infinite Studs: NOP'd sbb esi,edx at 0x%08X", STUD_SBB_ADDR);
    }
    
    g_stud_patched = 1;
    LOG("Infinite Studs: patched");
}

void remove_stud_patch(void) {
    if (!g_stud_patched) return;
    patch_mem(STUD_SUB_ADDR, g_stud_sub_orig, 2);
    patch_mem(STUD_SBB_ADDR, g_stud_sbb_orig, 2);
    g_stud_patched = 0;
    LOG("Infinite Studs: unpatched");
}

/* ================================================================
 * Invincibility: NOP the FSUB instruction that drains health.
 *
 * Health is a float at offset 0x864 in the player entity struct.
 * The game uses FPU instructions to decrement health each frame:
 *
 *   FLD  dword ptr [reg+0x864]   ; load current health   (D9 8? 64 08 00 00)
 *   FSUB dword ptr [delta_addr]  ; subtract frame delta  (D8 2? <addr>)
 *   FSTP dword ptr [reg+0x864]   ; store new health      (D9 9? 64 08 00 00)
 *
 * We find each FSUB that follows a FLD health and NOP it entirely.
 * This prevents passive health drain (damage over time) while keeping
 * the load/store intact so the game doesn't crash.
 *
 * Note: PATTERN 5 (INC byte [reg+0x864]) is an effect counter, NOT
 * health — do NOT patch it or the game crashes.
 * ================================================================ */
#define MAX_HEALTH_PATCHES 16
static DWORD g_health_addrs[MAX_HEALTH_PATCHES];
static unsigned char g_health_origs[MAX_HEALTH_PATCHES][8]; /* up to 8 bytes per FSUB */
static int g_health_sizes[MAX_HEALTH_PATCHES];
static int g_health_count = 0;
static int g_health_patched = 0;

/* Compute total length of a ModR/M instruction starting at code[0]=opcode, code[1]=ModR/M.
 * Accounts for optional SIB byte and displacement based on mod and r/m fields. */
static int modrm_instr_len(const unsigned char *code) {
    int modrm = code[1];
    int mod = (modrm >> 6) & 3;
    int rm  = modrm & 7;
    if (mod == 3) return 2;                      /* reg-reg: no memory operand */
    if (mod == 0 && rm == 4) {                   /* SIB byte follows */
        int base = code[2] & 7;
        return (base == 5) ? 7 : 3;             /* SIB+disp32 or SIB-only */
    }
    if (mod == 0 && rm == 5) return 6;           /* [disp32] direct */
    if (mod == 0) return 2;                      /* [reg] */
    if (mod == 1) return 3;                      /* [reg+disp8] */
    return 6;                                     /* [reg+disp32] */
}

static void scan_health_decrements(void) {
    if (g_health_count > 0) return;
    DWORD text_start, text_size;
    if (!find_text_section(&text_start, &text_size)) return;

    unsigned char *code = (unsigned char*)text_start;

    /* Scan for FLD dword ptr [reg+0x864]:
     *   D9 8? 64 08 00 00
     * ModR/M 0x8? = mod=10(disp32), reg=0(FLD st(0)), r/m=any base register.
     * This pattern is unique to health field access (offset 0x864). */
    for (DWORD i = 0; i + 20 < text_size && g_health_count < MAX_HEALTH_PATCHES; i++) {
        if (code[i] != 0xD9) continue;
        if ((code[i+1] & 0xF8) != 0x80) continue;    /* mod=10, reg=0 (FLD) */
        DWORD disp = *(DWORD*)(code + i + 2);
        if (disp != HEALTH_OFFSET) continue;

        /* FLD health found.  Scan forward up to 32 bytes for the FSUB.
         * FSUB is D8 /4: D8 opcode + ModR/M with reg field = 4 and mod != 11.
         * Known encoding: D8 25 <addr32> (FSUB dword ptr [disp32] for frame delta). */
        for (DWORD j = i + 6; j < i + 32 && j + 6 < text_size; j++) {
            if (code[j] != 0xD8) continue;
            int mod = (code[j+1] >> 6) & 3;
            int reg = (code[j+1] >> 3) & 7;
            if (mod == 3 || reg != 4) continue;        /* not a memory FSUB */

            int fsub_len = modrm_instr_len(code + j);
            if (fsub_len < 2 || j + (DWORD)fsub_len > text_size) continue;

            g_health_addrs[g_health_count] = text_start + j;
            g_health_sizes[g_health_count] = fsub_len;
            memcpy(g_health_origs[g_health_count], code + j, fsub_len);
            g_health_count++;
            break;  /* one FSUB per FLD health */
        }
    }
    LOG("Health decrements (FPU): found %d FSUB sites", g_health_count);
}

void apply_health_patch(void) {
    if (g_health_patched) return;
    scan_health_decrements();
    unsigned char nops[8] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
    for (int i = 0; i < g_health_count; i++) {
        patch_mem(g_health_addrs[i], nops, g_health_sizes[i]);
    }
    g_health_patched = 1;
    LOG("Invincibility: patched %d FSUB locations", g_health_count);
}

void remove_health_patch(void) {
    if (!g_health_patched) return;
    for (int i = 0; i < g_health_count; i++) {
        patch_mem(g_health_addrs[i], g_health_origs[i], g_health_sizes[i]);
    }
    g_health_patched = 0;
    LOG("Invincibility: unpatched");
}

void force_custom_studs(void) {
    if (!g_cheats.force_custom_studs) return;
    DWORD addr = STUD_DISPLAY_ADDR;
    DWORD old;
    if (VirtualProtect((LPVOID)addr, 4, PAGE_READWRITE, &old)) {
        *(int*)addr = g_cheats.custom_stud_value;
        VirtualProtect((LPVOID)addr, 4, old, &old);
    } else {
        static int logged = 0;
        if (!logged) {
            LOG("Custom studs: VirtualProtect FAILED at 0x%08X", addr);
            logged = 1;
        }
    }
}

void force_golden_bricks(void) {
    if (!g_cheats.force_golden_bricks) return;
    DWORD base = (DWORD)GetModuleHandleA(NULL);
    DWORD addr = base + GOLDEN_BRICK_OFFSET;
    DWORD old;
    if (VirtualProtect((LPVOID)addr, 4, PAGE_READWRITE, &old)) {
        *(int*)addr = g_cheats.golden_brick_value;
        VirtualProtect((LPVOID)addr, 4, old, &old);
    }
}

static float g_speed_walk_orig = SPEED_WALK_DEFAULT;
static float g_speed_run_orig  = SPEED_RUN_DEFAULT;

void apply_super_speed(void) {
    DWORD old;
    if (VirtualProtect((LPVOID)SPEED_WALK_ADDR, 4, PAGE_READWRITE, &old)) {
        *(float*)SPEED_WALK_ADDR = SPEED_WALK_CHEAT;
        VirtualProtect((LPVOID)SPEED_WALK_ADDR, 4, old, &old);
    }
    if (VirtualProtect((LPVOID)SPEED_RUN_ADDR, 4, PAGE_READWRITE, &old)) {
        *(float*)SPEED_RUN_ADDR = SPEED_RUN_CHEAT;
        VirtualProtect((LPVOID)SPEED_RUN_ADDR, 4, old, &old);
    }
}

void remove_super_speed(void) {
    DWORD old;
    if (VirtualProtect((LPVOID)SPEED_WALK_ADDR, 4, PAGE_READWRITE, &old)) {
        *(float*)SPEED_WALK_ADDR = g_speed_walk_orig;
        VirtualProtect((LPVOID)SPEED_WALK_ADDR, 4, old, &old);
    }
    if (VirtualProtect((LPVOID)SPEED_RUN_ADDR, 4, PAGE_READWRITE, &old)) {
        *(float*)SPEED_RUN_ADDR = g_speed_run_orig;
        VirtualProtect((LPVOID)SPEED_RUN_ADDR, 4, old, &old);
    }
}

/* ================================================================
 * Character Scale: patch FLD [reg+0xFB0] to redirect to our scale var.
 * Pattern: D9 8? B0 0F 00 00  (FLD dword ptr [reg+0xFB0])
 * ================================================================ */
#define MAX_SCALE_PATCHES 64
static float g_scale_val = 1.0f;
static struct {
    DWORD addr;
    unsigned char orig[6];
} g_scale_patches[MAX_SCALE_PATCHES];
static int g_scale_patch_count = 0;
static int g_scale_patched = 0;

static void scan_scale_patches(void) {
    if (g_scale_patch_count > 0) return;
    DWORD text_start, text_size;
    if (!find_text_section(&text_start, &text_size)) { return; }
    unsigned char *code = (unsigned char*)text_start;
    for (DWORD i = 0; i + 6 < text_size && g_scale_patch_count < MAX_SCALE_PATCHES; i++) {
        if (code[i] == 0xD9 &&
            ((code[i+1] >= 0x80 && code[i+1] <= 0x83) ||
             (code[i+1] >= 0x88 && code[i+1] <= 0x8B)) &&
            code[i+2] == 0xB0 && code[i+3] == 0x0F &&
            code[i+4] == 0x00 && code[i+5] == 0x00) {
            g_scale_patches[g_scale_patch_count].addr = text_start + i;
            memcpy(g_scale_patches[g_scale_patch_count].orig, code + i, 6);
            g_scale_patch_count++;
        }
    }
    LOG("Char Scale: found %d FLD [reg+0xFB0] instances", g_scale_patch_count);
}

static void apply_scale_val(float val) {
    g_scale_val = val;
    if (g_scale_patched) return;
    scan_scale_patches();
    if (g_scale_patch_count == 0) return;
    unsigned char patch[6];
    patch[0] = 0xD9; patch[1] = 0x05;
    *(DWORD*)&patch[2] = (DWORD)&g_scale_val;
    for (int i = 0; i < g_scale_patch_count; i++)
        patch_mem(g_scale_patches[i].addr, patch, 6);
    g_scale_patched = 1;
}

static void remove_scale_patches(void) {
    if (!g_scale_patched) return;
    for (int i = 0; i < g_scale_patch_count; i++)
        patch_mem(g_scale_patches[i].addr, g_scale_patches[i].orig, 6);
    g_scale_patched = 0;
}

void apply_char_scale(void) {
    static int prev = 0;
    if (!prev) { LOG("Char Scale: on"); prev = 1; }
    apply_scale_val((float)g_cheats.char_scale_val / 100.0f);
}

void remove_char_scale(void) {
    LOG("Char Scale: off");
    remove_scale_patches();
}

/* ================================================================
 * Y-Velocity: patch FMUL [reg+0xD78] to redirect to our own
 * gravity factor. Pattern: D8 8? 78 0D 00 00.
 * Slider -10..+10 controls vertical movement rate.
 * ================================================================ */
#define MAX_GRAVITY_PATCHES 256
static float g_gravity_mult = 1.0f;
static struct {
    DWORD addr;
    unsigned char orig[6];
} g_gravity_patches[MAX_GRAVITY_PATCHES];
static int g_gravity_patch_count = 0;
static int g_gravity_patched = 0;

static void scan_gravity_patches(void) {
    if (g_gravity_patch_count > 0) return;
    DWORD text_start, text_size;
    if (!find_text_section(&text_start, &text_size)) {
        LOG("Y-Velocity: failed to find .text section");
        return;
    }
    unsigned char *code = (unsigned char*)text_start;
    for (DWORD i = 0; i + 6 < text_size && g_gravity_patch_count < MAX_GRAVITY_PATCHES; i++) {
        if (code[i] == 0xD8 &&
            ((code[i+1] >= 0x88 && code[i+1] <= 0x8B) ||
             (code[i+1] >= 0x8D && code[i+1] <= 0x8F)) &&
            code[i+2] == 0x78 && code[i+3] == 0x0D && 
            code[i+4] == 0x00 && code[i+5] == 0x00) {
            g_gravity_patches[g_gravity_patch_count].addr = text_start + i;
            memcpy(g_gravity_patches[g_gravity_patch_count].orig, code + i, 6);
            g_gravity_patch_count++;
        }
    }
    LOG("Y-Velocity: found %d FMUL [reg+0xD78] instances", g_gravity_patch_count);
}

static void apply_gravity_mult(float mult) {
    g_gravity_mult = mult;
    if (g_gravity_patched) return;
    scan_gravity_patches();
    if (g_gravity_patch_count == 0) return;
    unsigned char patch[6];
    patch[0] = 0xD8; patch[1] = 0x0D;
    *(DWORD*)&patch[2] = (DWORD)&g_gravity_mult;
    for (int i = 0; i < g_gravity_patch_count; i++)
        patch_mem(g_gravity_patches[i].addr, patch, 6);
    g_gravity_patched = 1;
}

static void remove_gravity_mult(void) {
    if (!g_gravity_patched) return;
    for (int i = 0; i < g_gravity_patch_count; i++)
        patch_mem(g_gravity_patches[i].addr, g_gravity_patches[i].orig, 6);
    g_gravity_patched = 0;
}

void apply_super_jump(void) {
    static int prev = 0;
    if (!prev) { LOG("Y-Velocity: %.1f", (float)g_cheats.super_jump_scale / 10.0f); prev = 1; }
    apply_gravity_mult((float)g_cheats.super_jump_scale / 10.0f);
}

void remove_super_jump(void) {
    LOG("Y-Velocity: off");
    remove_gravity_mult();
}

const char *g_entities[64];
int g_entity_count = 0;

void scan_entities(void) {
    DWORD base = (DWORD)GetModuleHandleA(NULL);
    if (!base) return;
    DWORD *table = (DWORD*)(base + ENTITY_TABLE_OFFSET);
    g_entity_count = 0;
    for (int i = 0; i < 1024 && g_entity_count < 64; i++) {
        const char *str = (const char*)table[i];
        if (!str || IsBadReadPtr(str, 4)) continue;
        if (str[0] >= 32 && str[0] < 127 && strlen(str) > 2 && strlen(str) < 48) {
            g_entities[g_entity_count++] = str;
        }
    }
    if (g_entity_count > 0) {
        LOG("Entities: %d", g_entity_count);
    }
}

void update_cheats(void) {
    static int prev_time_freeze = 0;
    if (g_cheats.infinite_studs) apply_stud_patch(); else remove_stud_patch();
    if (g_cheats.invincible) apply_health_patch(); else remove_health_patch();
    if (g_cheats.super_speed) apply_super_speed(); else remove_super_speed();
    if (g_cheats.super_jump) apply_super_jump(); else remove_super_jump();
    if (g_cheats.char_scale) apply_char_scale(); else remove_char_scale();
    if (g_gravity_patched)
        g_gravity_mult = (float)g_cheats.super_jump_scale / 10.0f;
    if (g_scale_patched)
        g_scale_val = (float)g_cheats.char_scale_val / 100.0f;

    if (g_cheats.time_freeze && !prev_time_freeze) {
        time_freeze_snapshot();
        LOG("Time Freeze ON");
    }
    prev_time_freeze = g_cheats.time_freeze;

    force_custom_studs();
    force_golden_bricks();
}
