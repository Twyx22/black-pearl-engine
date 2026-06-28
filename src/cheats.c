#include "cheats.h"
#include "utils.h"
#include "hooks.h"
#include "config.h"
#include "water.h"
#include "noclip.h"
#include "infinite_ammo.h"
#include "score_mult.h"
#include "stud_magnet.h"

CheatsState g_cheats = {0, 0, 0, 0, 100, 1, 0, 0, 0, 0, 0, 1, CUSTOM_STUD_DEFAULT, 1, GOLDEN_BRICK_DEFAULT, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 75, 0, 0, 0, 0, 0};

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

static unsigned char g_health_dmg_pat[] = {0xFE, 0x8D, 0x26, 0x0E, 0x00, 0x00};
static unsigned char g_health_death_pat[] = {0x88, 0x8E, 0x26, 0x0E, 0x00, 0x00};
static PatchRecord g_health_dmg_rec = {0};
static PatchRecord g_health_death_rec = {0};
static AobCache g_health_dmg_cache = {0};
static AobCache g_health_death_cache = {0};

void apply_health_patch(void) {
    if (g_health_dmg_rec.active && g_health_death_rec.active) return;

    /* Resolve damage patch address */
    if (!g_health_dmg_rec.addr) {
        g_health_dmg_cache.pattern = g_health_dmg_pat;
        g_health_dmg_cache.mask = "xxxxxx";
        g_health_dmg_cache.len = 6;
        DWORD addr = find_pattern_cached(&g_health_dmg_cache);
        if (!addr) {
            DWORD base = (DWORD)GetModuleHandleA(NULL);
            addr = base + DAMAGE_PATCH_OFFSET;
            LOG("Invincibility: damage pattern not found, using fallback 0x%08X", addr);
        } else {
            LOG("Invincibility: damage signature resolved dynamically at 0x%08X", addr);
        }
        g_health_dmg_rec.addr = addr;
        g_health_dmg_rec.size = DAMAGE_PATCH_SIZE;
    }

    /* Resolve death patch address */
    if (!g_health_death_rec.addr) {
        g_health_death_cache.pattern = g_health_death_pat;
        g_health_death_cache.mask = "xxxxxx";
        g_health_death_cache.len = 6;
        DWORD addr = find_pattern_cached(&g_health_death_cache);
        if (!addr) {
            DWORD base = (DWORD)GetModuleHandleA(NULL);
            addr = base + DEATH_PATCH_OFFSET;
            LOG("Invincibility: death pattern not found, using fallback 0x%08X", addr);
        } else {
            LOG("Invincibility: death signature resolved dynamically at 0x%08X", addr);
        }
        g_health_death_rec.addr = addr;
        g_health_death_rec.size = DEATH_PATCH_SIZE;
    }

    /* NOP the damage instruction: DEC [EBP+0E26] -> NOP x6 */
    unsigned char nops_dmg[DAMAGE_PATCH_SIZE];
    memset(nops_dmg, 0x90, DAMAGE_PATCH_SIZE);
    if (!patch_apply(&g_health_dmg_rec, nops_dmg)) {
        LOG("Invincibility: failed to patch damage at 0x%08X", g_health_dmg_rec.addr);
        return;
    }
    LOG("Invincibility: NOP'd DEC [EBP+0E26] at 0x%08X (enemy damage disabled)", g_health_dmg_rec.addr);

    /* NOP the death instruction: MOV [ESI+0E26],CL -> NOP x6 */
    unsigned char nops_death[DEATH_PATCH_SIZE];
    memset(nops_death, 0x90, DEATH_PATCH_SIZE);
    if (!patch_apply(&g_health_death_rec, nops_death)) {
        /* Rollback damage patch */
        patch_restore(&g_health_dmg_rec);
        LOG("Invincibility: failed to patch death at 0x%08X, damage rolled back", g_health_death_rec.addr);
        return;
    }
    LOG("Invincibility: NOP'd MOV [ESI+0E26],CL at 0x%08X (death health reset disabled)", g_health_death_rec.addr);

    LOG("Invincibility: patched (enemy damage + death reset disabled)");
}
void remove_health_patch(void) {
    int had_dmg = g_health_dmg_rec.active;
    int had_death = g_health_death_rec.active;
    if (had_death) patch_restore(&g_health_death_rec);
    if (had_dmg) patch_restore(&g_health_dmg_rec);
    if (had_dmg || had_death) LOG("Invincibility: unpatched");
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

static unsigned char g_rev_damage_pat[] = {0xFE, 0x8D, 0x26, 0x0E, 0x00, 0x00};
static PatchRecord g_rev_damage_rec = {0};
static AobCache g_rev_damage_cache = {0};

void apply_reverse_damage(void) {
    if (g_rev_damage_rec.active) return;

    if (!g_rev_damage_rec.addr) {
        g_rev_damage_cache.pattern = g_rev_damage_pat;
        g_rev_damage_cache.mask = "xxxxxx";
        g_rev_damage_cache.len = 6;
        DWORD addr = find_pattern_cached(&g_rev_damage_cache);
        if (!addr) {
            DWORD base = (DWORD)GetModuleHandleA(NULL);
            addr = base + REVERSE_DAMAGE_OFFSET;
            LOG("Reverse Damage: pattern not found, using fallback 0x%08X", addr);
        } else {
            LOG("Reverse Damage: signature resolved dynamically at 0x%08X", addr);
        }
        g_rev_damage_rec.addr = addr;
        g_rev_damage_rec.size = REVERSE_DAMAGE_SIZE;
    }

    /* Build patch: DEC -> INC (change byte 1: 0x8D -> 0x8C) */
    unsigned char patch[REVERSE_DAMAGE_SIZE];
    patch[0] = 0xFE; patch[1] = 0x8C;
    patch[2] = 0x26; patch[3] = 0x0E;
    patch[4] = 0x00; patch[5] = 0x00;

    if (!patch_apply(&g_rev_damage_rec, patch)) {
        LOG("Reverse Damage: patch_apply FAILED at 0x%08X", g_rev_damage_rec.addr);
        return;
    }
    LOG("Reverse Damage: DEC->INC at 0x%08X (heal on hit)", g_rev_damage_rec.addr);
}
void remove_reverse_damage(void) {
    if (!g_rev_damage_rec.active) return;
    patch_restore(&g_rev_damage_rec);
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

static unsigned char g_heart4_pat[] = {0xC6, 0x86, 0x26, 0x0E, 0x00, 0x00, 0x04};
static unsigned char g_heart3_pat[] = {0xC6, 0x86, 0x26, 0x0E, 0x00, 0x00, 0x03};
static PatchRecord g_heart_rec4 = {0};
static PatchRecord g_heart_rec3 = {0};
static AobCache g_heart4_cache = {0};
static AobCache g_heart3_cache = {0};

void apply_one_heart(void) {
    if (g_heart_rec4.active && g_heart_rec3.active) return;

    /* Resolve init4 address */
    if (!g_heart_rec4.addr) {
        g_heart4_cache.pattern = g_heart4_pat;
        g_heart4_cache.mask = "xxxxxxx";
        g_heart4_cache.len = 7;
        DWORD addr = find_pattern_cached(&g_heart4_cache);
        if (!addr) {
            DWORD base = (DWORD)GetModuleHandleA(NULL);
            addr = base + HEALTH_INIT_4_OFFSET;
            LOG("One Heart: init4 pattern not found, using fallback 0x%08X", addr);
        } else {
            LOG("One Heart: init4 signature resolved dynamically at 0x%08X", addr);
        }
        g_heart_rec4.addr = addr;
        g_heart_rec4.size = HEALTH_INIT_SIZE;
    }

    /* Resolve init3 address */
    if (!g_heart_rec3.addr) {
        g_heart3_cache.pattern = g_heart3_pat;
        g_heart3_cache.mask = "xxxxxxx";
        g_heart3_cache.len = 7;
        DWORD addr = find_pattern_cached(&g_heart3_cache);
        if (!addr) {
            DWORD base = (DWORD)GetModuleHandleA(NULL);
            addr = base + HEALTH_INIT_3_OFFSET;
            LOG("One Heart: init3 pattern not found, using fallback 0x%08X", addr);
        } else {
            LOG("One Heart: init3 signature resolved dynamically at 0x%08X", addr);
        }
        g_heart_rec3.addr = addr;
        g_heart_rec3.size = HEALTH_INIT_SIZE;
    }

    /* Patch init4: change byte 6 from 0x04 to 0x01 */
    unsigned char p4[HEALTH_INIT_SIZE];
    memcpy(p4, g_heart4_pat, HEALTH_INIT_SIZE);
    p4[6] = 0x01;
    if (!patch_apply(&g_heart_rec4, p4)) {
        LOG("One Heart: failed to patch init4 at 0x%08X", g_heart_rec4.addr);
        return;
    }

    /* Patch init3: change byte 6 from 0x03 to 0x01 */
    unsigned char p3[HEALTH_INIT_SIZE];
    memcpy(p3, g_heart3_pat, HEALTH_INIT_SIZE);
    p3[6] = 0x01;
    if (!patch_apply(&g_heart_rec3, p3)) {
        patch_restore(&g_heart_rec4);
        LOG("One Heart: failed to patch init3 at 0x%08X, init4 rolled back", g_heart_rec3.addr);
        return;
    }

    LOG("One Heart: patched init4 (0x%08X) and init3 (0x%08X) to 1 HP",
        g_heart_rec4.addr, g_heart_rec3.addr);
}
void remove_one_heart(void) {
    int had4 = g_heart_rec4.active;
    int had3 = g_heart_rec3.active;
    if (had3) patch_restore(&g_heart_rec3);
    if (had4) patch_restore(&g_heart_rec4);
    if (had4 || had3)
        LOG("One Heart: unpatched");
}

/* ================================================================
 * No Knockback: NOP the CALL to Knockback within TakeDamage.
 *
 * At KNOCKBACK_CALL_OFFSET (0x3D3A5F):
 *   Original: E8 BC 95 FF FF  = CALL sub_7CD020 (Knockback)
 *   Patch:    90 90 90 90 90  = NOP x5
 * ================================================================ */

static unsigned char g_knockback_pat[] = {0xE8, 0xBC, 0x95, 0xFF, 0xFF};
static PatchRecord g_knockback_rec = {0};
static AobCache g_knockback_cache = {0};

void apply_no_knockback(void) {
    if (g_knockback_rec.active) return;

    if (!g_knockback_rec.addr) {
        g_knockback_cache.pattern = g_knockback_pat;
        g_knockback_cache.mask = "xxxxx";
        g_knockback_cache.len = 5;
        DWORD addr = find_pattern_cached(&g_knockback_cache);
        if (!addr) {
            DWORD base = (DWORD)GetModuleHandleA(NULL);
            addr = base + KNOCKBACK_CALL_OFFSET;
            LOG("No Knockback: pattern not found, using fallback 0x%08X", addr);
        } else {
            LOG("No Knockback: signature resolved dynamically at 0x%08X", addr);
        }
        g_knockback_rec.addr = addr;
        g_knockback_rec.size = KNOCKBACK_CALL_SIZE;
    }

    unsigned char nops[KNOCKBACK_CALL_SIZE];
    memset(nops, 0x90, KNOCKBACK_CALL_SIZE);
    if (!patch_apply(&g_knockback_rec, nops)) {
        LOG("No Knockback: patch_apply FAILED at 0x%08X", g_knockback_rec.addr);
        return;
    }
    LOG("No Knockback: NOP'd CALL at 0x%08X", g_knockback_rec.addr);
}
void remove_no_knockback(void) {
    if (!g_knockback_rec.active) return;
    patch_restore(&g_knockback_rec);
    LOG("No Knockback: unpatched");
}

/* ================================================================
 * No Hit Reactions: NOP the CALL to HitReaction7.
 *
 * At HITREACT_CALL_OFFSET (0x052415):
 *   Original: E8 46 C8 FF FF  = CALL sub_44EC60 (HitReaction7)
 *   Patch:    90 90 90 90 90  = NOP x5
 * ================================================================ */

static unsigned char g_hitreact_pat[] = {0xE8, 0x46, 0xC8, 0xFF, 0xFF};
static PatchRecord g_hitreact_rec = {0};
static AobCache g_hitreact_cache = {0};

void apply_no_hit_reaction(void) {
    if (g_hitreact_rec.active) return;

    if (!g_hitreact_rec.addr) {
        g_hitreact_cache.pattern = g_hitreact_pat;
        g_hitreact_cache.mask = "xxxxx";
        g_hitreact_cache.len = 5;
        DWORD addr = find_pattern_cached(&g_hitreact_cache);
        if (!addr) {
            DWORD base = (DWORD)GetModuleHandleA(NULL);
            addr = base + HITREACT_CALL_OFFSET;
            LOG("No Hit Reaction: pattern not found, using fallback 0x%08X", addr);
        } else {
            LOG("No Hit Reaction: signature resolved dynamically at 0x%08X", addr);
        }
        g_hitreact_rec.addr = addr;
        g_hitreact_rec.size = HITREACT_CALL_SIZE;
    }

    unsigned char nops[HITREACT_CALL_SIZE];
    memset(nops, 0x90, HITREACT_CALL_SIZE);
    if (!patch_apply(&g_hitreact_rec, nops)) {
        LOG("No Hit Reaction: patch_apply FAILED at 0x%08X", g_hitreact_rec.addr);
        return;
    }
    LOG("No Hit Reaction: NOP'd CALL at 0x%08X", g_hitreact_rec.addr);
}
void remove_no_hit_reaction(void) {
    if (!g_hitreact_rec.active) return;
    patch_restore(&g_hitreact_rec);
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

static unsigned char g_dmgresp_pat[] = {0xFE, 0x8D, 0x26, 0x0E, 0x00, 0x00};
static PatchRecord g_dmgresp_rec = {0};
static AobCache g_dmgresp_cache = {0};

void apply_damage_response_only(void) {
    if (g_dmgresp_rec.active) return;

    if (!g_dmgresp_rec.addr) {
        g_dmgresp_cache.pattern = g_dmgresp_pat;
        g_dmgresp_cache.mask = "xxxxxx";
        g_dmgresp_cache.len = 6;
        DWORD addr = find_pattern_cached(&g_dmgresp_cache);
        if (!addr) {
            DWORD base = (DWORD)GetModuleHandleA(NULL);
            addr = base + DAMAGE_PATCH_OFFSET;
            LOG("Damage Response: pattern not found, using fallback 0x%08X", addr);
        } else {
            LOG("Damage Response: signature resolved dynamically at 0x%08X", addr);
        }
        g_dmgresp_rec.addr = addr;
        g_dmgresp_rec.size = DAMAGE_PATCH_SIZE;
    }

    unsigned char nops[DAMAGE_PATCH_SIZE];
    memset(nops, 0x90, DAMAGE_PATCH_SIZE);
    if (!patch_apply(&g_dmgresp_rec, nops)) {
        LOG("Damage Response: patch_apply FAILED at 0x%08X", g_dmgresp_rec.addr);
        return;
    }
    LOG("Damage Response Only: NOP'd DEC at 0x%08X (no damage, FX still play)", g_dmgresp_rec.addr);
}
void remove_damage_response_only(void) {
    if (!g_dmgresp_rec.active) return;
    patch_restore(&g_dmgresp_rec);
    LOG("Damage Response Only: unpatched");
}

/* ================================================================
 * One-Hit-Kill: One Heart Mode + NOP Death Health Reset.
 *
 * With 1 HP, any single hit kills the player.
 * NOPing the death reset prevents health from being restored to 1.
 * ================================================================ */

static unsigned char g_ohk_death_pat[] = {0x88, 0x8E, 0x26, 0x0E, 0x00, 0x00};
static PatchRecord g_ohk_death_rec = {0};
static AobCache g_ohk_death_cache = {0};

void apply_one_hit_kill(void) {
    apply_one_heart();

    if (g_ohk_death_rec.active) return;

    if (!g_ohk_death_rec.addr) {
        g_ohk_death_cache.pattern = g_ohk_death_pat;
        g_ohk_death_cache.mask = "xxxxxx";
        g_ohk_death_cache.len = 6;
        DWORD addr = find_pattern_cached(&g_ohk_death_cache);
        if (!addr) {
            DWORD base = (DWORD)GetModuleHandleA(NULL);
            addr = base + DEATH_PATCH_OFFSET;
            LOG("One-Hit-Kill: death pattern not found, using fallback 0x%08X", addr);
        } else {
            LOG("One-Hit-Kill: death signature resolved dynamically at 0x%08X", addr);
        }
        g_ohk_death_rec.addr = addr;
        g_ohk_death_rec.size = DEATH_PATCH_SIZE;
    }

    unsigned char nops[DEATH_PATCH_SIZE];
    memset(nops, 0x90, DEATH_PATCH_SIZE);
    if (!patch_apply(&g_ohk_death_rec, nops)) {
        LOG("One-Hit-Kill: patch_apply FAILED for death at 0x%08X", g_ohk_death_rec.addr);
        return;
    }
    LOG("One-Hit-Kill: NOP'd death reset at 0x%08X", g_ohk_death_rec.addr);
}
void remove_one_hit_kill(void) {
    remove_one_heart();

    if (!g_ohk_death_rec.active) return;
    patch_restore(&g_ohk_death_rec);
    LOG("One-Hit-Kill: death reset restored");
}

void update_cheats(void) {
    static CheatsState g_prev = {0};

    /* Dirty flag: skip patch apply/remove unless state changed.
     * Force-writes (custom studs, golden bricks) still run every frame. */
    if (memcmp(&g_cheats, &g_prev, sizeof(CheatsState)) == 0) {
        force_custom_studs();
        force_golden_bricks();
        return;
    }

    /* Detect rising edges for one-shot actions */
    int time_freeze_rising = g_cheats.time_freeze && !g_prev.time_freeze;

    g_prev = g_cheats;

    /* Core toggles (non-damage) */
    if (g_cheats.infinite_studs) apply_stud_patch(); else remove_stud_patch();
    if (g_cheats.super_speed) apply_super_speed(); else remove_super_speed();
    if (g_cheats.super_jump) apply_super_jump(); else remove_super_jump();
    if (g_cheats.char_scale) apply_char_scale(); else remove_char_scale();
    if (g_cheats.underwater_breath) apply_breath_patch(); else remove_breath_patch();

    /* Update scale/gravity multipliers when already patched */
    if (g_gravity_patched)
        g_gravity_mult = (float)g_cheats.super_jump_scale / 10.0f;
    if (g_scale_patched)
        g_scale_val = (float)g_cheats.char_scale_val / 100.0f;

    /* ================================================================
     * DAMAGE SYSTEM - Mutual Exclusion Rules
     *
     * DAMAGE_PATCH_OFFSET  (DEC [reg+0xE26]): shared by 3 cheats
     *   Priority: Invincibility > Damage Response Only > Reverse Damage
     *   (Only one can be active at a time - they patch the same address)
     *
     * DEATH_PATCH_OFFSET   (MOV [reg+0xE26], CL): shared by 2 cheats
     *   Priority: Invincibility > One-Hit-Kill
     *
     * Combo: One-Hit-Kill = One Heart + Death NOP
     *   (One Heart is always applied as part of OHK)
     * ================================================================ */

    if (g_cheats.invincible) {
        /* Highest priority: patches both damage + death addresses */
        apply_health_patch();
        remove_damage_response_only();
        remove_reverse_damage();
        remove_one_hit_kill();
        if (g_cheats.damage_response_only || g_cheats.reverse_damage || g_cheats.one_hit_kill)
            LOG("Damage: invincible active - suppressed conflicting cheat(s)");
    } else if (g_cheats.one_hit_kill) {
        /* Second priority: patches death address */
        apply_one_hit_kill();
        remove_health_patch();
        remove_damage_response_only();
        remove_reverse_damage();
        if (g_cheats.damage_response_only || g_cheats.reverse_damage)
            LOG("Damage: one_hit_kill active - suppressed conflicting cheat(s)");
    } else if (g_cheats.damage_response_only) {
        /* Third priority: patches damage address only */
        apply_damage_response_only();
        remove_health_patch();
        remove_reverse_damage();
        remove_one_hit_kill();
        if (g_cheats.reverse_damage)
            LOG("Damage: damage_response_only active - suppressed conflicting cheat(s)");
    } else if (g_cheats.reverse_damage) {
        /* Lowest priority on damage address */
        apply_reverse_damage();
        remove_health_patch();
        remove_damage_response_only();
        remove_one_hit_kill();
    } else {
        /* No conflicting damage mods - ensure all clean */
        remove_health_patch();
        remove_damage_response_only();
        remove_reverse_damage();
        remove_one_hit_kill();
    }

    /* Independent damage cheats (unique addresses - no conflicts) */
    if (g_cheats.one_heart) {
        apply_one_heart();
    } else {
        remove_one_heart();
    }
    if (g_cheats.no_knockback) apply_no_knockback(); else remove_no_knockback();
    if (g_cheats.no_hit_reaction) apply_no_hit_reaction(); else remove_no_hit_reaction();

    /* Other systems */
    if (g_cheats.infinite_ammo) infinite_ammo_apply(); else infinite_ammo_remove();
    if (g_cheats.noclip) noclip_apply(); else noclip_remove();

    if (time_freeze_rising) {
        time_freeze_snapshot();
        LOG("Time Freeze ON");
    }

    force_custom_studs();
    force_golden_bricks();
    if (g_cheats.stud_magnet) stud_magnet_apply(); else stud_magnet_remove();
    water_set_enabled(g_cheats.remove_water);
    if (g_cheats.score_mult) score_mult_apply(); else score_mult_remove();
    if (g_cheats.show_debug) {
        scan_entities();
    }
}