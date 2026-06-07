#include "cheats.h"
#include "utils.h"
#include "hooks.h"
#include "config.h"
#include "water.h"

CheatsState g_cheats = {0, 0, 0, 0, 100, 1, 0, 0, 0, 0, 0, 1, CUSTOM_STUD_DEFAULT, 1, GOLDEN_BRICK_DEFAULT, 0, 0, 0, 100, 0, 0};

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

    DWORD base = (DWORD)GetModuleHandleA(NULL);
    g_damage_patch_addr = base + DAMAGE_PATCH_OFFSET;
    g_death_patch_addr  = base + DEATH_PATCH_OFFSET;

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
 *
 * Cheat Engine script (_LEGOPirates.exe+37B910):
 *   Original: FE 8E 36 03 00 00  = DEC [ESI+00000336]
 *   Effect: Decrements oxygen timer when underwater.
 *   Patch: NOP (6 bytes) = oxygen timer never decrements = infinite breath.
 * ================================================================ */

static DWORD g_breath_patch_addr = 0;
static unsigned char g_breath_orig[BREATH_PATCH_SIZE];
static int g_breath_patched = 0;

void apply_breath_patch(void) {
    if (g_breath_patched) return;

    DWORD base = (DWORD)GetModuleHandleA(NULL);
    g_breath_patch_addr = base + BREATH_PATCH_OFFSET;

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

    if (g_cheats.time_freeze && !prev_time_freeze) {
        time_freeze_snapshot();
        LOG("Time Freeze ON");
    }
    prev_time_freeze = g_cheats.time_freeze;

    force_custom_studs();
    force_golden_bricks();
    water_set_enabled(g_cheats.remove_water);
}
