#include "cheats.h"
#include "utils.h"
#include "hooks.h"
#include "config.h"

CheatsState g_cheats = {0, 0, 0, 0, 100, 1, 0, 0, 0, 0, 0, 1, CUSTOM_STUD_DEFAULT, 1, GOLDEN_BRICK_DEFAULT};

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

static DWORD g_health_addrs[16];
static unsigned char g_health_origs[16][4];
static int g_health_count = 0;
static int g_health_patched = 0;

static void scan_health_decrements(void) {
    if (g_health_count > 0) return;
    DWORD text_start, text_size;
    if (!find_text_section(&text_start, &text_size)) return;

    unsigned char *code = (unsigned char*)text_start;
    for (DWORD i = 0; i < text_size - 10 && g_health_count < 16; i++) {
        if (code[i] == 0xFE && (code[i+1] & 0xC0) == 0x80) {
            DWORD disp = *(DWORD*)(code + i + 2);
            if (disp == HEALTH_OFFSET) {
                g_health_addrs[g_health_count] = text_start + i;
                memcpy(g_health_origs[g_health_count], code + i, 4);
                g_health_count++;
            }
        }
        if (code[i] == 0x80 && (code[i+1] & 0xC0) == 0x80 && code[i+6] == 0x01) {
            DWORD disp = *(DWORD*)(code + i + 2);
            if (disp == HEALTH_OFFSET) {
                g_health_addrs[g_health_count] = text_start + i;
                memcpy(g_health_origs[g_health_count], code + i, 4);
                g_health_count++;
            }
        }
        if (code[i] == 0x83 && (code[i+1] & 0xC0) == 0x80 && code[i+6] == 0x01) {
            DWORD disp = *(DWORD*)(code + i + 2);
            if (disp == HEALTH_OFFSET) {
                g_health_addrs[g_health_count] = text_start + i;
                memcpy(g_health_origs[g_health_count], code + i, 4);
                g_health_count++;
            }
        }
    }
    LOG("Health decrements found: %d", g_health_count);
}

void apply_health_patch(void) {
    if (g_health_patched) return;
    scan_health_decrements();
    unsigned char nop4[4] = {0x90, 0x90, 0x90, 0x90};
    for (int i = 0; i < g_health_count; i++) {
        patch_mem(g_health_addrs[i], nop4, 4);
    }
    g_health_patched = 1;
    LOG("Invincibility: patched %d locations", g_health_count);
}

void remove_health_patch(void) {
    if (!g_health_patched) return;
    for (int i = 0; i < g_health_count; i++) {
        patch_mem(g_health_addrs[i], g_health_origs[i], 4);
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

/* Moon Jump: patch fmul [esi+0xD78] → fmul [our_gravity_float]
   to control the gravity multiplier for all entities. */
static float g_moon_mult = GRAVITY_MULT_MOON;
static unsigned char g_fmul1_orig[6] = {0};
static unsigned char g_fmul2_orig[6] = {0};
static int g_moon_patched = 0;

void apply_moon_jump(void) {
    if (g_moon_patched) return;
    DWORD base = (DWORD)GetModuleHandleA(NULL);
    DWORD addr1 = base + JUMP_FMUL_OFFSET1;
    DWORD addr2 = base + JUMP_FMUL_OFFSET2;

    /* Save original bytes */
    memcpy(g_fmul1_orig, (void*)addr1, 6);
    memcpy(g_fmul2_orig, (void*)addr2, 6);

    /* Build patch: D8 0D [addr] = fmul dword ptr [addr] (absolute) */
    unsigned char patch[6];
    patch[0] = 0xD8; patch[1] = 0x0D;
    *(DWORD*)&patch[2] = (DWORD)&g_moon_mult;

    DWORD old;
    if (VirtualProtect((LPVOID)addr1, 6, PAGE_EXECUTE_READWRITE, &old)) {
        memcpy((void*)addr1, patch, 6);
        VirtualProtect((LPVOID)addr1, 6, old, &old);
    }
    if (VirtualProtect((LPVOID)addr2, 6, PAGE_EXECUTE_READWRITE, &old)) {
        memcpy((void*)addr2, patch, 6);
        VirtualProtect((LPVOID)addr2, 6, old, &old);
    }
    g_moon_patched = 1;
    LOG("Moon Jump: fmul redirected to %.2f", g_moon_mult);
}

void remove_moon_jump(void) {
    if (!g_moon_patched) return;
    DWORD base = (DWORD)GetModuleHandleA(NULL);
    patch_mem(base + JUMP_FMUL_OFFSET1, g_fmul1_orig, 6);
    patch_mem(base + JUMP_FMUL_OFFSET2, g_fmul2_orig, 6);
    g_moon_patched = 0;
    LOG("Moon Jump: unpatched");
}

/* Super Jump: patch fld [esi+0xFB0] → fld [our_force_float]
   to override the jump/velocity input. */
static float g_super_jump_force = 100.0f;
static unsigned char g_fld1_orig[6] = {0};
static unsigned char g_fld2_orig[6] = {0};
static int g_super_jump_patched = 0;

void apply_super_jump(void) {
    g_super_jump_force = (float)g_cheats.super_jump_scale;
    if (g_super_jump_patched) return;
    DWORD base = (DWORD)GetModuleHandleA(NULL);
    DWORD addr1 = base + JUMP_FLD_OFFSET1;
    DWORD addr2 = base + JUMP_FLD_OFFSET2;

    memcpy(g_fld1_orig, (void*)addr1, 6);
    memcpy(g_fld2_orig, (void*)addr2, 6);

    /* Build patch: D9 05 [addr] = fld dword ptr [addr] (absolute) */
    unsigned char patch[6];
    patch[0] = 0xD9; patch[1] = 0x05;
    *(DWORD*)&patch[2] = (DWORD)&g_super_jump_force;

    DWORD old;
    if (VirtualProtect((LPVOID)addr1, 6, PAGE_EXECUTE_READWRITE, &old)) {
        memcpy((void*)addr1, patch, 6);
        VirtualProtect((LPVOID)addr1, 6, old, &old);
    }
    if (VirtualProtect((LPVOID)addr2, 6, PAGE_EXECUTE_READWRITE, &old)) {
        memcpy((void*)addr2, patch, 6);
        VirtualProtect((LPVOID)addr2, 6, old, &old);
    }
    g_super_jump_patched = 1;
    LOG("Super Jump: fld redirected to %.1f", g_super_jump_force);
}

void remove_super_jump(void) {
    if (!g_super_jump_patched) return;
    DWORD base = (DWORD)GetModuleHandleA(NULL);
    patch_mem(base + JUMP_FLD_OFFSET1, g_fld1_orig, 6);
    patch_mem(base + JUMP_FLD_OFFSET2, g_fld2_orig, 6);
    g_super_jump_patched = 0;
    LOG("Super Jump: unpatched");
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
    if (g_cheats.super_jump) { apply_super_jump(); g_super_jump_force = (float)g_cheats.super_jump_scale; } else remove_super_jump();
    if (g_cheats.moon_jump) apply_moon_jump(); else remove_moon_jump();

    if (g_cheats.time_freeze && !prev_time_freeze) {
        time_freeze_snapshot();
        LOG("Time Freeze ON");
    }
    prev_time_freeze = g_cheats.time_freeze;

    force_custom_studs();
    force_golden_bricks();
}
