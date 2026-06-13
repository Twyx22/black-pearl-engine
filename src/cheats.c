#include "cheats.h"
#include "utils.h"
#include "hooks.h"
#include "config.h"
#include "water.h"

CheatsState g_cheats = {0, 0, 0, 0, 100, 1, 0, 0, 0, 0, 0, 1, CUSTOM_STUD_DEFAULT, 1, GOLDEN_BRICK_DEFAULT, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0};

/* Stud patch: NOP sub ebx,eax and sbb esi,edx at fixed addresses (Cheat Engine found) */
static unsigned char g_stud_sub_orig[2] = {0};
static unsigned char g_stud_sbb_orig[2] = {0};
static int g_stud_patched = 0;
static DWORD g_stud_sub_addr = 0;
static DWORD g_stud_sbb_addr = 0;

void apply_stud_patch(void) {
    if (g_stud_patched) return;
    
    if (!g_stud_sub_addr) {
        unsigned char pattern[] = {0x29, 0xC3, 0x19, 0xD6}; // sub ebx, eax; sbb esi, edx
        DWORD addr = find_pattern(pattern, "xxxx", 4);
        if (addr) {
            g_stud_sub_addr = addr;
            g_stud_sbb_addr = addr + 2;
            LOG("Infinite Studs: signature resolved dynamically at 0x%08X", g_stud_sub_addr);
        } else {
            LOG("Infinite Studs: pattern not found, using fallback address");
            g_stud_sub_addr = STUD_SUB_ADDR;
            g_stud_sbb_addr = STUD_SBB_ADDR;
        }
    }
    
    /* Read original bytes */
    memcpy(g_stud_sub_orig, (void*)g_stud_sub_addr, 2);
    memcpy(g_stud_sbb_orig, (void*)g_stud_sbb_addr, 2);
    
    /* NOP sub ebx,eax (2 bytes) */
    unsigned char nop2[2] = {0x90, 0x90};
    DWORD old;
    if (VirtualProtect((LPVOID)g_stud_sub_addr, 2, PAGE_EXECUTE_READWRITE, &old)) {
        memcpy((void*)g_stud_sub_addr, nop2, 2);
        VirtualProtect((LPVOID)g_stud_sub_addr, 2, old, &old);
        LOG("Infinite Studs: NOP'd sub ebx,eax at 0x%08X", g_stud_sub_addr);
    } else {
        LOG("Infinite Studs: VirtualProtect FAILED at 0x%08X", g_stud_sub_addr);
        return;
    }
    
    /* NOP sbb esi,edx (2 bytes) */
    if (VirtualProtect((LPVOID)g_stud_sbb_addr, 2, PAGE_EXECUTE_READWRITE, &old)) {
        memcpy((void*)g_stud_sbb_addr, nop2, 2);
        VirtualProtect((LPVOID)g_stud_sbb_addr, 2, old, &old);
        LOG("Infinite Studs: NOP'd sbb esi,edx at 0x%08X", g_stud_sbb_addr);
    }
    
    g_stud_patched = 1;
    LOG("Infinite Studs: patched");
}

void remove_stud_patch(void) {
    if (!g_stud_patched) return;
    patch_mem(g_stud_sub_addr, g_stud_sub_orig, 2);
    patch_mem(g_stud_sbb_addr, g_stud_sbb_orig, 2);
    g_stud_patched = 0;
    LOG("Infinite Studs: unpatched");
}

/* ================================================================
 * Invincibility: Direct binary patches for enemy damage and death.
 *
 * Two patches based on Cheat Engine scripts:
 *
 * 1) DAMAGE PATCH (_LEGOPirates.exe+3D3B0B):
 *    Original: FE 8D 26 0E 00 00  = DEC [EBP+0E26]
 *    Effect: Decrements health when hit by enemy.
 *    Patch: NOP (6 bytes) = player takes 0 damage from enemies.
 *
 * 2) DEATH PATCH (_LEGOPirates.exe+4A3D35):
 *    Original: 88 8E 26 0E 00 00  = MOV [ESI+0E26],CL
 *    Effect: Resets health to 1 on death (CL=1).
 *    Patch: NOP (6 bytes) = health stays at 4 hearts on death.
 *
 * Health offset is 0xE26 in the player entity struct.
 * ================================================================ */

static DWORD g_damage_patch_addr = 0;
static DWORD g_death_patch_addr = 0;
static unsigned char g_damage_orig[DAMAGE_PATCH_SIZE];
static unsigned char g_death_orig[DEATH_PATCH_SIZE];
static int g_health_patched = 0;

void apply_health_patch(void) {
    if (g_health_patched) return;

    if (!g_damage_patch_addr) {
        unsigned char dmg_pattern[] = {0xFE, 0x8D, 0x26, 0x0E, 0x00, 0x00}; // DEC [ebp+0xE26]
        g_damage_patch_addr = find_pattern(dmg_pattern, "xxxxxx", 6);
        if (g_damage_patch_addr) {
            LOG("Invincibility: damage signature resolved dynamically at 0x%08X", g_damage_patch_addr);
        } else {
            LOG("Invincibility: damage pattern not found, using fallback");
            DWORD base = (DWORD)GetModuleHandleA(NULL);
            g_damage_patch_addr = base + DAMAGE_PATCH_OFFSET;
        }
    }
    if (!g_death_patch_addr) {
        unsigned char death_pattern[] = {0x88, 0x8E, 0x26, 0x0E, 0x00, 0x00}; // MOV [esi+0xE26], cl
        g_death_patch_addr = find_pattern(death_pattern, "xxxxxx", 6);
        if (g_death_patch_addr) {
            LOG("Invincibility: death signature resolved dynamically at 0x%08X", g_death_patch_addr);
        } else {
            LOG("Invincibility: death pattern not found, using fallback");
            DWORD base = (DWORD)GetModuleHandleA(NULL);
            g_death_patch_addr = base + DEATH_PATCH_OFFSET;
        }
    }

    DWORD old;
    unsigned char nops[6] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90};

    /* Save original bytes from both addresses */
    if (!VirtualProtect((LPVOID)g_damage_patch_addr, DAMAGE_PATCH_SIZE, PAGE_EXECUTE_READ, &old)) {
        LOG("Invincibility: cannot read damage patch target 0x%08X", g_damage_patch_addr);
        return;
    }
    memcpy(g_damage_orig, (void*)g_damage_patch_addr, DAMAGE_PATCH_SIZE);
    VirtualProtect((LPVOID)g_damage_patch_addr, DAMAGE_PATCH_SIZE, old, &old);

    if (!VirtualProtect((LPVOID)g_death_patch_addr, DEATH_PATCH_SIZE, PAGE_EXECUTE_READ, &old)) {
        LOG("Invincibility: cannot read death patch target 0x%08X", g_death_patch_addr);
        return;
    }
    memcpy(g_death_orig, (void*)g_death_patch_addr, DEATH_PATCH_SIZE);
    VirtualProtect((LPVOID)g_death_patch_addr, DEATH_PATCH_SIZE, old, &old);

    /* NOP the damage instruction: DEC [EBP+0E26] -> NOP x6 */
    if (!VirtualProtect((LPVOID)g_damage_patch_addr, DAMAGE_PATCH_SIZE, PAGE_EXECUTE_READWRITE, &old)) {
        LOG("Invincibility: VirtualProtect FAILED for damage patch at 0x%08X", g_damage_patch_addr);
        return;
    }
    memcpy((void*)g_damage_patch_addr, nops, DAMAGE_PATCH_SIZE);
    VirtualProtect((LPVOID)g_damage_patch_addr, DAMAGE_PATCH_SIZE, old, &old);
    LOG("Invincibility: NOP'd DEC [EBP+0E26] at 0x%08X (enemy damage disabled)", g_damage_patch_addr);

    /* NOP the death instruction: MOV [ESI+0E26],CL -> NOP x6 */
    if (!VirtualProtect((LPVOID)g_death_patch_addr, DEATH_PATCH_SIZE, PAGE_EXECUTE_READWRITE, &old)) {
        LOG("Invincibility: VirtualProtect FAILED for death patch at 0x%08X", g_death_patch_addr);
        return;
    }
    memcpy((void*)g_death_patch_addr, nops, DEATH_PATCH_SIZE);
    VirtualProtect((LPVOID)g_death_patch_addr, DEATH_PATCH_SIZE, old, &old);
    LOG("Invincibility: NOP'd MOV [ESI+0E26],CL at 0x%08X (death health reset disabled)", g_death_patch_addr);

    g_health_patched = 1;
    LOG("Invincibility: patched (enemy damage + death reset disabled)");
}

void remove_health_patch(void) {
    if (!g_health_patched) return;

    DWORD old;

    /* Restore damage instruction */
    if (VirtualProtect((LPVOID)g_damage_patch_addr, DAMAGE_PATCH_SIZE, PAGE_EXECUTE_READWRITE, &old)) {
        memcpy((void*)g_damage_patch_addr, g_damage_orig, DAMAGE_PATCH_SIZE);
        VirtualProtect((LPVOID)g_damage_patch_addr, DAMAGE_PATCH_SIZE, old, &old);
    }

    /* Restore death instruction */
    if (VirtualProtect((LPVOID)g_death_patch_addr, DEATH_PATCH_SIZE, PAGE_EXECUTE_READWRITE, &old)) {
        memcpy((void*)g_death_patch_addr, g_death_orig, DEATH_PATCH_SIZE);
        VirtualProtect((LPVOID)g_death_patch_addr, DEATH_PATCH_SIZE, old, &old);
    }

    g_health_patched = 0;
    LOG("Invincibility: unpatched");
}


/* ================================================================
 * Breathe Underwater: NOP oxygen timer decrement.
 * ================================================================ */

static DWORD g_breath_patch_addr = 0;
static unsigned char g_breath_orig[BREATH_PATCH_SIZE];
static int g_breath_patched = 0;

void apply_breath_patch(void) {
    if (g_breath_patched) return;

    if (!g_breath_patch_addr) {
        unsigned char breath_pattern[] = {0xFE, 0x8E, 0x36, 0x03, 0x00, 0x00}; // DEC [esi+0x336]
        g_breath_patch_addr = find_pattern(breath_pattern, "xxxxxx", 6);
        if (g_breath_patch_addr) {
            LOG("Breathe Underwater: signature resolved dynamically at 0x%08X", g_breath_patch_addr);
        } else {
            LOG("Breathe Underwater: pattern not found, using fallback");
            DWORD base = (DWORD)GetModuleHandleA(NULL);
            g_breath_patch_addr = base + BREATH_PATCH_OFFSET;
        }
    }

    DWORD old;
    unsigned char nops[6] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90};

    /* Save original bytes */
    if (!VirtualProtect((LPVOID)g_breath_patch_addr, BREATH_PATCH_SIZE, PAGE_EXECUTE_READ, &old)) {
        LOG("Breathe Underwater: cannot read patch target 0x%08X", g_breath_patch_addr);
        return;
    }
    memcpy(g_breath_orig, (void*)g_breath_patch_addr, BREATH_PATCH_SIZE);
    VirtualProtect((LPVOID)g_breath_patch_addr, BREATH_PATCH_SIZE, old, &old);

    /* NOP the oxygen decrement: DEC [ESI+336] -> NOP x6 */
    if (!VirtualProtect((LPVOID)g_breath_patch_addr, BREATH_PATCH_SIZE, PAGE_EXECUTE_READWRITE, &old)) {
        LOG("Breathe Underwater: VirtualProtect FAILED at 0x%08X", g_breath_patch_addr);
        return;
    }
    memcpy((void*)g_breath_patch_addr, nops, BREATH_PATCH_SIZE);
    VirtualProtect((LPVOID)g_breath_patch_addr, BREATH_PATCH_SIZE, old, &old);
    LOG("Breathe Underwater: NOP'd DEC [ESI+336] at 0x%08X (oxygen timer frozen)", g_breath_patch_addr);

    g_breath_patched = 1;
    LOG("Breathe Underwater: patched");
}

void remove_breath_patch(void) {
    if (!g_breath_patched) return;

    DWORD old;

    /* Restore original instruction */
    if (VirtualProtect((LPVOID)g_breath_patch_addr, BREATH_PATCH_SIZE, PAGE_EXECUTE_READWRITE, &old)) {
        memcpy((void*)g_breath_patch_addr, g_breath_orig, BREATH_PATCH_SIZE);
        VirtualProtect((LPVOID)g_breath_patch_addr, BREATH_PATCH_SIZE, old, &old);
    }

    g_breath_patched = 0;
    LOG("Breathe Underwater: unpatched");
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

/* ================================================================
 * Reverse Damage (Heal on Hit): DEC -> INC (1 byte change).
 *
 * At DAMAGE_PATCH_OFFSET (0x3D3B0B):
 *   Original: FE 8D 26 0E 00 00  = DEC byte [EBP+0E26]
 *   Patch:    FE 8C 26 0E 00 00  = INC byte [EBP+0E26]
 *   Only the ModR/M byte changes: 0x8D -> 0x8C.
 * ================================================================ */

static DWORD g_rev_damage_addr = 0;
static unsigned char g_rev_damage_orig[REVERSE_DAMAGE_SIZE];
static int g_rev_damage_patched = 0;

void apply_reverse_damage(void) {
    if (g_rev_damage_patched) return;

    DWORD base = (DWORD)GetModuleHandleA(NULL);
    g_rev_damage_addr = base + REVERSE_DAMAGE_OFFSET;

    DWORD old;

    /* Save original bytes */
    if (!VirtualProtect((LPVOID)g_rev_damage_addr, REVERSE_DAMAGE_SIZE, PAGE_EXECUTE_READ, &old)) {
        LOG("Reverse Damage: cannot read target 0x%08X", g_rev_damage_addr);
        return;
    }
    memcpy(g_rev_damage_orig, (void*)g_rev_damage_addr, REVERSE_DAMAGE_SIZE);
    VirtualProtect((LPVOID)g_rev_damage_addr, REVERSE_DAMAGE_SIZE, old, &old);

    /* Patch: change 0x8D to 0x8C (DEC -> INC) */
    unsigned char patched[REVERSE_DAMAGE_SIZE];
    memcpy(patched, g_rev_damage_orig, REVERSE_DAMAGE_SIZE);
    patched[1] = 0x8C;

    if (!VirtualProtect((LPVOID)g_rev_damage_addr, REVERSE_DAMAGE_SIZE, PAGE_EXECUTE_READWRITE, &old)) {
        LOG("Reverse Damage: VirtualProtect FAILED at 0x%08X", g_rev_damage_addr);
        return;
    }
    memcpy((void*)g_rev_damage_addr, patched, REVERSE_DAMAGE_SIZE);
    VirtualProtect((LPVOID)g_rev_damage_addr, REVERSE_DAMAGE_SIZE, old, &old);
    LOG("Reverse Damage: DEC->INC at 0x%08X (heal on hit)", g_rev_damage_addr);

    g_rev_damage_patched = 1;
}

void remove_reverse_damage(void) {
    if (!g_rev_damage_patched) return;

    DWORD old;
    if (VirtualProtect((LPVOID)g_rev_damage_addr, REVERSE_DAMAGE_SIZE, PAGE_EXECUTE_READWRITE, &old)) {
        memcpy((void*)g_rev_damage_addr, g_rev_damage_orig, REVERSE_DAMAGE_SIZE);
        VirtualProtect((LPVOID)g_rev_damage_addr, REVERSE_DAMAGE_SIZE, old, &old);
    }

    g_rev_damage_patched = 0;
    LOG("Reverse Damage: unpatched");
}


/* ================================================================
 * One Heart Mode: patch health init values to 1.
 *
 * At HEALTH_INIT_4_OFFSET (0x2BC97A):
 *   Original: C6 86 26 0E 00 00 04  = MOV byte [ESI+0E26], 4
 *   Patch:    C6 86 26 0E 00 00 01  = MOV byte [ESI+0E26], 1
 *
 * At HEALTH_INIT_3_OFFSET (0x4877FC):
 *   Original: C6 86 26 0E 00 00 03  = MOV byte [ESI+0E26], 3
 *   Patch:    C6 86 26 0E 00 00 01  = MOV byte [ESI+0E26], 1
 * ================================================================ */

static DWORD g_heart_addr4 = 0, g_heart_addr3 = 0;
static unsigned char g_heart_orig4[HEALTH_INIT_SIZE], g_heart_orig3[HEALTH_INIT_SIZE];
static int g_one_heart_patched = 0;

void apply_one_heart(void) {
    if (g_one_heart_patched) return;

    DWORD base = (DWORD)GetModuleHandleA(NULL);
    g_heart_addr4 = base + HEALTH_INIT_4_OFFSET;
    g_heart_addr3 = base + HEALTH_INIT_3_OFFSET;

    DWORD old;

    /* Save originals */
    if (!VirtualProtect((LPVOID)g_heart_addr4, HEALTH_INIT_SIZE, PAGE_EXECUTE_READ, &old)) {
        LOG("One Heart: cannot read init4 target 0x%08X", g_heart_addr4);
        return;
    }
    memcpy(g_heart_orig4, (void*)g_heart_addr4, HEALTH_INIT_SIZE);
    VirtualProtect((LPVOID)g_heart_addr4, HEALTH_INIT_SIZE, old, &old);

    if (!VirtualProtect((LPVOID)g_heart_addr3, HEALTH_INIT_SIZE, PAGE_EXECUTE_READ, &old)) {
        LOG("One Heart: cannot read init3 target 0x%08X", g_heart_addr3);
        return;
    }
    memcpy(g_heart_orig3, (void*)g_heart_addr3, HEALTH_INIT_SIZE);
    VirtualProtect((LPVOID)g_heart_addr3, HEALTH_INIT_SIZE, old, &old);

    /* Patch init4: 04 -> 01 */
    unsigned char p4[HEALTH_INIT_SIZE];
    memcpy(p4, g_heart_orig4, HEALTH_INIT_SIZE);
    p4[6] = 0x01;

    if (!VirtualProtect((LPVOID)g_heart_addr4, HEALTH_INIT_SIZE, PAGE_EXECUTE_READWRITE, &old)) {
        LOG("One Heart: VirtualProtect FAILED at 0x%08X", g_heart_addr4);
        return;
    }
    memcpy((void*)g_heart_addr4, p4, HEALTH_INIT_SIZE);
    VirtualProtect((LPVOID)g_heart_addr4, HEALTH_INIT_SIZE, old, &old);

    /* Patch init3: 03 -> 01 */
    unsigned char p3[HEALTH_INIT_SIZE];
    memcpy(p3, g_heart_orig3, HEALTH_INIT_SIZE);
    p3[6] = 0x01;

    if (!VirtualProtect((LPVOID)g_heart_addr3, HEALTH_INIT_SIZE, PAGE_EXECUTE_READWRITE, &old)) {
        LOG("One Heart: VirtualProtect FAILED at 0x%08X", g_heart_addr3);
        return;
    }
    memcpy((void*)g_heart_addr3, p3, HEALTH_INIT_SIZE);
    VirtualProtect((LPVOID)g_heart_addr3, HEALTH_INIT_SIZE, old, &old);

    g_one_heart_patched = 1;
    LOG("One Heart: patched init4 (0x%08X) and init3 (0x%08X) to 1 HP", g_heart_addr4, g_heart_addr3);
}

void remove_one_heart(void) {
    if (!g_one_heart_patched) return;

    DWORD old;
    if (VirtualProtect((LPVOID)g_heart_addr4, HEALTH_INIT_SIZE, PAGE_EXECUTE_READWRITE, &old)) {
        memcpy((void*)g_heart_addr4, g_heart_orig4, HEALTH_INIT_SIZE);
        VirtualProtect((LPVOID)g_heart_addr4, HEALTH_INIT_SIZE, old, &old);
    }
    if (VirtualProtect((LPVOID)g_heart_addr3, HEALTH_INIT_SIZE, PAGE_EXECUTE_READWRITE, &old)) {
        memcpy((void*)g_heart_addr3, g_heart_orig3, HEALTH_INIT_SIZE);
        VirtualProtect((LPVOID)g_heart_addr3, HEALTH_INIT_SIZE, old, &old);
    }

    g_one_heart_patched = 0;
    LOG("One Heart: unpatched");
}


/* ================================================================
 * No Knockback: NOP the CALL to Knockback within TakeDamage.
 *
 * At KNOCKBACK_CALL_OFFSET (0x3D3A5F):
 *   Original: E8 BC 95 FF FF  = CALL sub_7CD020 (Knockback)
 *   Patch:    90 90 90 90 90  = NOP x5
 * ================================================================ */

static DWORD g_knockback_addr = 0;
static unsigned char g_knockback_orig[KNOCKBACK_CALL_SIZE];
static int g_knockback_patched = 0;

void apply_no_knockback(void) {
    if (g_knockback_patched) return;

    DWORD base = (DWORD)GetModuleHandleA(NULL);
    g_knockback_addr = base + KNOCKBACK_CALL_OFFSET;

    DWORD old;

    if (!VirtualProtect((LPVOID)g_knockback_addr, KNOCKBACK_CALL_SIZE, PAGE_EXECUTE_READ, &old)) {
        LOG("No Knockback: cannot read target 0x%08X", g_knockback_addr);
        return;
    }
    memcpy(g_knockback_orig, (void*)g_knockback_addr, KNOCKBACK_CALL_SIZE);
    VirtualProtect((LPVOID)g_knockback_addr, KNOCKBACK_CALL_SIZE, old, &old);

    unsigned char nops[5] = {0x90, 0x90, 0x90, 0x90, 0x90};
    if (!VirtualProtect((LPVOID)g_knockback_addr, KNOCKBACK_CALL_SIZE, PAGE_EXECUTE_READWRITE, &old)) {
        LOG("No Knockback: VirtualProtect FAILED at 0x%08X", g_knockback_addr);
        return;
    }
    memcpy((void*)g_knockback_addr, nops, KNOCKBACK_CALL_SIZE);
    VirtualProtect((LPVOID)g_knockback_addr, KNOCKBACK_CALL_SIZE, old, &old);

    g_knockback_patched = 1;
    LOG("No Knockback: NOP'd CALL at 0x%08X", g_knockback_addr);
}

void remove_no_knockback(void) {
    if (!g_knockback_patched) return;

    DWORD old;
    if (VirtualProtect((LPVOID)g_knockback_addr, KNOCKBACK_CALL_SIZE, PAGE_EXECUTE_READWRITE, &old)) {
        memcpy((void*)g_knockback_addr, g_knockback_orig, KNOCKBACK_CALL_SIZE);
        VirtualProtect((LPVOID)g_knockback_addr, KNOCKBACK_CALL_SIZE, old, &old);
    }

    g_knockback_patched = 0;
    LOG("No Knockback: unpatched");
}


/* ================================================================
 * No Hit Reactions: NOP the CALL to HitReaction7.
 *
 * At HITREACT_CALL_OFFSET (0x052415):
 *   Original: E8 46 C8 FF FF  = CALL sub_44EC60 (HitReaction7)
 *   Patch:    90 90 90 90 90  = NOP x5
 * ================================================================ */

static DWORD g_hitreact_addr = 0;
static unsigned char g_hitreact_orig[HITREACT_CALL_SIZE];
static int g_hitreact_patched = 0;

void apply_no_hit_reaction(void) {
    if (g_hitreact_patched) return;

    DWORD base = (DWORD)GetModuleHandleA(NULL);
    g_hitreact_addr = base + HITREACT_CALL_OFFSET;

    DWORD old;

    if (!VirtualProtect((LPVOID)g_hitreact_addr, HITREACT_CALL_SIZE, PAGE_EXECUTE_READ, &old)) {
        LOG("No Hit Reaction: cannot read target 0x%08X", g_hitreact_addr);
        return;
    }
    memcpy(g_hitreact_orig, (void*)g_hitreact_addr, HITREACT_CALL_SIZE);
    VirtualProtect((LPVOID)g_hitreact_addr, HITREACT_CALL_SIZE, old, &old);

    unsigned char nops[5] = {0x90, 0x90, 0x90, 0x90, 0x90};
    if (!VirtualProtect((LPVOID)g_hitreact_addr, HITREACT_CALL_SIZE, PAGE_EXECUTE_READWRITE, &old)) {
        LOG("No Hit Reaction: VirtualProtect FAILED at 0x%08X", g_hitreact_addr);
        return;
    }
    memcpy((void*)g_hitreact_addr, nops, HITREACT_CALL_SIZE);
    VirtualProtect((LPVOID)g_hitreact_addr, HITREACT_CALL_SIZE, old, &old);

    g_hitreact_patched = 1;
    LOG("No Hit Reaction: NOP'd CALL at 0x%08X", g_hitreact_addr);
}

void remove_no_hit_reaction(void) {
    if (!g_hitreact_patched) return;

    DWORD old;
    if (VirtualProtect((LPVOID)g_hitreact_addr, HITREACT_CALL_SIZE, PAGE_EXECUTE_READWRITE, &old)) {
        memcpy((void*)g_hitreact_addr, g_hitreact_orig, HITREACT_CALL_SIZE);
        VirtualProtect((LPVOID)g_hitreact_addr, HITREACT_CALL_SIZE, old, &old);
    }

    g_hitreact_patched = 0;
    LOG("No Hit Reaction: unpatched");
}


/* ================================================================
 * Damage Response Only: NOP the DEC (no damage taken) but leave
 * the death health reset active. All hit FX still play.
 *
 * Uses the same damage patch as invincibility (DAMAGE_PATCH_OFFSET).
 * The key difference: the death reset patch is NOT touched.
 *
 * NOTE: Mutually exclusive with Invincibility - both patch the
 * same address. Only enable one at a time.
 * ================================================================ */

static DWORD g_dmgresp_addr = 0;
static unsigned char g_dmgresp_orig[DAMAGE_PATCH_SIZE];
static int g_dmgresp_patched = 0;

void apply_damage_response_only(void) {
    if (g_dmgresp_patched) return;

    DWORD base = (DWORD)GetModuleHandleA(NULL);
    g_dmgresp_addr = base + DAMAGE_PATCH_OFFSET;

    DWORD old;

    if (!VirtualProtect((LPVOID)g_dmgresp_addr, DAMAGE_PATCH_SIZE, PAGE_EXECUTE_READ, &old)) {
        LOG("Damage Response: cannot read target 0x%08X", g_dmgresp_addr);
        return;
    }
    memcpy(g_dmgresp_orig, (void*)g_dmgresp_addr, DAMAGE_PATCH_SIZE);
    VirtualProtect((LPVOID)g_dmgresp_addr, DAMAGE_PATCH_SIZE, old, &old);

    unsigned char nops[6] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
    if (!VirtualProtect((LPVOID)g_dmgresp_addr, DAMAGE_PATCH_SIZE, PAGE_EXECUTE_READWRITE, &old)) {
        LOG("Damage Response: VirtualProtect FAILED at 0x%08X", g_dmgresp_addr);
        return;
    }
    memcpy((void*)g_dmgresp_addr, nops, DAMAGE_PATCH_SIZE);
    VirtualProtect((LPVOID)g_dmgresp_addr, DAMAGE_PATCH_SIZE, old, &old);

    g_dmgresp_patched = 1;
    LOG("Damage Response Only: NOP'd DEC at 0x%08X (no damage, FX still play)", g_dmgresp_addr);
}

void remove_damage_response_only(void) {
    if (!g_dmgresp_patched) return;

    DWORD old;
    if (VirtualProtect((LPVOID)g_dmgresp_addr, DAMAGE_PATCH_SIZE, PAGE_EXECUTE_READWRITE, &old)) {
        memcpy((void*)g_dmgresp_addr, g_dmgresp_orig, DAMAGE_PATCH_SIZE);
        VirtualProtect((LPVOID)g_dmgresp_addr, DAMAGE_PATCH_SIZE, old, &old);
    }

    g_dmgresp_patched = 0;
    LOG("Damage Response Only: unpatched");
}


/* ================================================================
 * One-Hit-Kill: One Heart Mode + NOP Death Health Reset.
 *
 * With 1 HP, any single hit kills the player.
 * NOPing the death reset prevents health from being restored to 1.
 * ================================================================ */

static DWORD g_ohk_death_addr = 0;
static unsigned char g_ohk_death_orig[DEATH_PATCH_SIZE];
static int g_ohk_death_patched = 0;

void apply_one_hit_kill(void) {
    apply_one_heart();

    if (g_ohk_death_patched) return;

    DWORD base = (DWORD)GetModuleHandleA(NULL);
    g_ohk_death_addr = base + DEATH_PATCH_OFFSET;

    DWORD old;

    /* Save original death bytes */
    if (!VirtualProtect((LPVOID)g_ohk_death_addr, DEATH_PATCH_SIZE, PAGE_EXECUTE_READ, &old)) {
        LOG("One-Hit-Kill: cannot read death target 0x%08X", g_ohk_death_addr);
        return;
    }
    memcpy(g_ohk_death_orig, (void*)g_ohk_death_addr, DEATH_PATCH_SIZE);
    VirtualProtect((LPVOID)g_ohk_death_addr, DEATH_PATCH_SIZE, old, &old);

    /* NOP the death reset */
    unsigned char nops[6] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
    if (!VirtualProtect((LPVOID)g_ohk_death_addr, DEATH_PATCH_SIZE, PAGE_EXECUTE_READWRITE, &old)) {
        LOG("One-Hit-Kill: VirtualProtect FAILED for death at 0x%08X", g_ohk_death_addr);
        return;
    }
    memcpy((void*)g_ohk_death_addr, nops, DEATH_PATCH_SIZE);
    VirtualProtect((LPVOID)g_ohk_death_addr, DEATH_PATCH_SIZE, old, &old);

    g_ohk_death_patched = 1;
    LOG("One-Hit-Kill: NOP'd death reset at 0x%08X", g_ohk_death_addr);
}

void remove_one_hit_kill(void) {
    remove_one_heart();

    if (!g_ohk_death_patched) return;

    DWORD old;
    if (VirtualProtect((LPVOID)g_ohk_death_addr, DEATH_PATCH_SIZE, PAGE_EXECUTE_READWRITE, &old)) {
        memcpy((void*)g_ohk_death_addr, g_ohk_death_orig, DEATH_PATCH_SIZE);
        VirtualProtect((LPVOID)g_ohk_death_addr, DEATH_PATCH_SIZE, old, &old);
    }

    g_ohk_death_patched = 0;
    LOG("One-Hit-Kill: death reset restored");
}


void update_cheats(void) {
    static int prev_time_freeze = 0;
    if (g_cheats.infinite_studs) apply_stud_patch(); else remove_stud_patch();
    if (g_cheats.invincible) apply_health_patch(); else remove_health_patch();
    if (g_cheats.underwater_breath) apply_breath_patch(); else remove_breath_patch();
    if (g_cheats.super_speed) apply_super_speed(); else remove_super_speed();
    if (g_cheats.super_jump) apply_super_jump(); else remove_super_jump();
    if (g_cheats.char_scale) apply_char_scale(); else remove_char_scale();
    if (g_gravity_patched)
        g_gravity_mult = (float)g_cheats.super_jump_scale / 10.0f;
    if (g_scale_patched)
        g_scale_val = (float)g_cheats.char_scale_val / 100.0f;

    /* Damage system mods */
    if (g_cheats.reverse_damage) apply_reverse_damage(); else remove_reverse_damage();
    if (g_cheats.one_heart) apply_one_heart(); else remove_one_heart();
    if (g_cheats.no_knockback) apply_no_knockback(); else remove_no_knockback();
    if (g_cheats.no_hit_reaction) apply_no_hit_reaction(); else remove_no_hit_reaction();
    if (g_cheats.damage_response_only) apply_damage_response_only(); else remove_damage_response_only();
    if (g_cheats.one_hit_kill) apply_one_hit_kill(); else remove_one_hit_kill();

    if (g_cheats.time_freeze && !prev_time_freeze) {
        time_freeze_snapshot();
        LOG("Time Freeze ON");
    }
    prev_time_freeze = g_cheats.time_freeze;

    force_custom_studs();
    force_golden_bricks();
    water_set_enabled(g_cheats.remove_water);
    if (g_cheats.show_debug) {
        scan_entities();
    }
}
