#include "cheats.h"
#include "utils.h"
#include "hooks.h"

CheatsState g_cheats = {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 999999};

#define STUD_COUNTER_ADDR 0x00E41868
#define MAX_STUD_PATCHES 32
static DWORD g_stud_patch_addrs[MAX_STUD_PATCHES];
static unsigned char g_stud_patch_origs[MAX_STUD_PATCHES][10];
static int g_stud_patch_count = 0;
static int g_stud_patched = 0;

static void scan_stud_subtractions(void) {
    if (g_stud_patch_count > 0) return;
    DWORD text_start, text_size;
    if (!find_text_section(&text_start, &text_size)) return;

    unsigned char *code = (unsigned char*)text_start;
    for (DWORD i = 0; i < text_size - 10 && g_stud_patch_count < MAX_STUD_PATCHES; i++) {
        if (code[i] == 0x81 && (code[i+1] & 0xC7) == 0x05) {
            DWORD disp = *(DWORD*)(code + i + 2);
            if (disp == STUD_COUNTER_ADDR) {
                g_stud_patch_addrs[g_stud_patch_count] = text_start + i;
                memcpy(g_stud_patch_origs[g_stud_patch_count], code + i, 10);
                g_stud_patch_count++;
            }
        }
        if (code[i] == 0x83 && (code[i+1] & 0xC7) == 0x05) {
            DWORD disp = *(DWORD*)(code + i + 2);
            if (disp == STUD_COUNTER_ADDR) {
                g_stud_patch_addrs[g_stud_patch_count] = text_start + i;
                memcpy(g_stud_patch_origs[g_stud_patch_count], code + i, 7);
                g_stud_patch_count++;
            }
        }
    }
    LOG("Stud subtractions found: %d", g_stud_patch_count);
}

void apply_stud_patch(void) {
    if (g_stud_patched) return;
    scan_stud_subtractions();
    unsigned char nops[10] = {0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90};
    for (int i = 0; i < g_stud_patch_count; i++) {
        int len = 10;
        unsigned char *p = g_stud_patch_origs[i];
        if (p[0] == 0x83) len = 7;
        patch_mem(g_stud_patch_addrs[i], nops, len);
    }
    g_stud_patched = 1;
    LOG("Infinite Studs: patched %d locations", g_stud_patch_count);
}

void remove_stud_patch(void) {
    if (!g_stud_patched) return;
    for (int i = 0; i < g_stud_patch_count; i++) {
        int len = 10;
        unsigned char *p = g_stud_patch_origs[i];
        if (p[0] == 0x83) len = 7;
        patch_mem(g_stud_patch_addrs[i], g_stud_patch_origs[i], len);
    }
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
            if (disp == 0x864) {
                g_health_addrs[g_health_count] = text_start + i;
                memcpy(g_health_origs[g_health_count], code + i, 4);
                g_health_count++;
            }
        }
        if (code[i] == 0x80 && (code[i+1] & 0xC0) == 0x80 && code[i+6] == 0x01) {
            DWORD disp = *(DWORD*)(code + i + 2);
            if (disp == 0x864) {
                g_health_addrs[g_health_count] = text_start + i;
                memcpy(g_health_origs[g_health_count], code + i, 4);
                g_health_count++;
            }
        }
        if (code[i] == 0x83 && (code[i+1] & 0xC0) == 0x80 && code[i+6] == 0x01) {
            DWORD disp = *(DWORD*)(code + i + 2);
            if (disp == 0x864) {
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

#define STUD_DISPLAY_ADDR 0x0359B660

void force_custom_studs(void) {
    if (!g_cheats.force_custom_studs) return;
    DWORD addr = STUD_DISPLAY_ADDR;
    DWORD old;
    if (VirtualProtect((LPVOID)addr, 4, PAGE_READWRITE, &old)) {
        *(int*)addr = g_cheats.custom_stud_value;
        VirtualProtect((LPVOID)addr, 4, old, &old);
    }
}

#define SPEED_WALK_ADDR 0x00E24C50
#define SPEED_RUN_ADDR  0x00E24C54
static float g_speed_walk_orig = 3.0f;
static float g_speed_run_orig  = 3.5f;

void apply_super_speed(void) {
    DWORD old;
    if (VirtualProtect((LPVOID)SPEED_WALK_ADDR, 4, PAGE_READWRITE, &old)) {
        *(float*)SPEED_WALK_ADDR = 300.0f;
        VirtualProtect((LPVOID)SPEED_WALK_ADDR, 4, old, &old);
    }
    if (VirtualProtect((LPVOID)SPEED_RUN_ADDR, 4, PAGE_READWRITE, &old)) {
        *(float*)SPEED_RUN_ADDR = 350.0f;
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

#define ENTITY_TABLE_OFF 0x00C8F400
const char *g_entities[64];
int g_entity_count = 0;

void scan_entities(void) {
    DWORD base = (DWORD)GetModuleHandleA(NULL);
    if (!base) return;
    DWORD *table = (DWORD*)(base + ENTITY_TABLE_OFF);
    g_entity_count = 0;
    for (int i = 0; i < 1024 && g_entity_count < 64; i++) {
        const char *str = (const char*)table[i];
        if (!str || IsBadReadPtr(str, 4)) continue;
        if (str[0] >= 32 && str[0] < 127 && strlen(str) > 2 && strlen(str) < 48) {
            g_entities[g_entity_count++] = str;
        }
    }
    LOG("Entities: %d", g_entity_count);
}

void update_cheats(void) {
    static int prev_time_freeze = 0;
    if (g_cheats.infinite_studs) apply_stud_patch(); else remove_stud_patch();
    if (g_cheats.invincible) apply_health_patch(); else remove_health_patch();
    if (g_cheats.super_speed) apply_super_speed(); else remove_super_speed();

    if (g_cheats.time_freeze && !prev_time_freeze) {
        time_freeze_snapshot();
        LOG("Time Freeze ON");
    }
    prev_time_freeze = g_cheats.time_freeze;

    force_custom_studs();
}
