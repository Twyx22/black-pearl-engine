#include "config_loader.h"
#include "cheats.h"
#include "favorites.h"
#include "hotkeys.h"
#include "utils.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ================================================================
 * Config V2 — Section-based INI format with auto-migration
 *
 * New format:
 *   [Cheats]
 *   invincible=1
 *   infinite_studs=0
 *   ...
 *
 *   [Favorites]
 *   favorite_0=2,5    (tab, item)
 *   ...
 *
 *   [Hotkeys]
 *   hotkey_0=0x46,3,1  (vk, tab, item)
 *   ...
 *
 * Old format (flat) is auto-detected and migrated on first write.
 * ================================================================ */

typedef struct {
    const char *name;
    int *field;
} ConfigMapping;

#define MIGRATION_MARKER "# bpe_cfg_v2"

static ConfigMapping g_map[] = {
    {"infinite_studs",      &g_cheats.infinite_studs},
    {"invincible",          &g_cheats.invincible},
    {"super_speed",         &g_cheats.super_speed},
    {"super_jump",          &g_cheats.super_jump},
    {"super_jump_scale",    &g_cheats.super_jump_scale},
    {"speed_mult",          &g_cheats.speed_mult},
    {"time_freeze",         &g_cheats.time_freeze},
    {"noclip",              &g_cheats.noclip},
    {"show_debug",          &g_cheats.show_debug},
    {"show_fps",            &g_cheats.show_fps},
    {"score_mult",          &g_cheats.score_mult},
    {"force_custom_studs",  &g_cheats.force_custom_studs},
    {"custom_stud_value",   &g_cheats.custom_stud_value},
    {"force_golden_bricks", &g_cheats.force_golden_bricks},
    {"golden_brick_value",  &g_cheats.golden_brick_value},
    {"uw_enabled",          &g_cheats.uw_enabled},
    {"uw_ratio",            &g_cheats.uw_ratio},
    {"char_scale",          &g_cheats.char_scale},
    {"char_scale_val",      &g_cheats.char_scale_val},
    {"remove_water",        &g_cheats.remove_water},
    {"underwater_breath",    &g_cheats.underwater_breath},
    {"reverse_damage",       &g_cheats.reverse_damage},
    {"one_heart",            &g_cheats.one_heart},
    {"no_knockback",         &g_cheats.no_knockback},
    {"no_hit_reaction",      &g_cheats.no_hit_reaction},
    {"damage_response_only", &g_cheats.damage_response_only},
    {"one_hit_kill",         &g_cheats.one_hit_kill},
    {"infinite_ammo",        &g_cheats.infinite_ammo},
    {"stud_magnet",          &g_cheats.stud_magnet},
    /* New v5 fields */
    {"quick_combo",          &g_cheats.quick_combo},
    {"super_punch",          &g_cheats.super_punch},
    {"always_gold",          &g_cheats.always_gold},
    {"mega_destruct",        &g_cheats.mega_destruct},
    {"infinite_cannonballs", &g_cheats.infinite_cannonballs},
    {"free_camera",          &g_cheats.free_camera},
    {"fov",                  &g_cheats.fov},
    {"teleport_slot_0",      &g_cheats.teleport_slot_0},
    {"teleport_slot_1",      &g_cheats.teleport_slot_1},
    {"teleport_slot_2",      &g_cheats.teleport_slot_2},
    {"teleport_slot_3",      &g_cheats.teleport_slot_3},
    {"teleport_slot_4",      &g_cheats.teleport_slot_4},
};

#define MAP_COUNT (sizeof(g_map) / sizeof(g_map[0]))

static ConfigMapping *find_mapping(const char *name) {
    for (int i = 0; i < MAP_COUNT; i++) {
        if (strcmp(g_map[i].name, name) == 0)
            return &g_map[i];
    }
    return NULL;
}

/* -----------------------------------------------------------------
 * Check if the config file is already V2 format (contains section headers)
 * ----------------------------------------------------------------- */
static int is_v2_format(FILE *f) {
    char line[256];
    long pos = ftell(f);
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '[') {
            fseek(f, pos, SEEK_SET);
            return 1;
        }
        if (*p == '#' && strstr(p, MIGRATION_MARKER)) {
            fseek(f, pos, SEEK_SET);
            return 1;
        }
    }
    fseek(f, pos, SEEK_SET);
    return 0;
}

/* -----------------------------------------------------------------
 * Parse a key=value line (handles whitespace trimming)
 * ----------------------------------------------------------------- */
static int parse_kv_line(const char *line, char *name, int name_sz, int *value) {
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '#' || *p == ';' || *p == '\n' || *p == '\r' || *p == '\0')
        return 0;
    if (*p == '[') return 0;  /* Section header — skip */

    const char *eq = strchr(p, '=');
    if (!eq) return 0;

    /* Extract name */
    int nlen = (int)(eq - p);
    if (nlen >= name_sz) nlen = name_sz - 1;
    memcpy(name, p, nlen);
    name[nlen] = '\0';

    /* Trim trailing whitespace from name */
    char *end = name + nlen - 1;
    while (end >= name && (*end == ' ' || *end == '\t')) *end-- = '\0';

    /* Parse value */
    const char *v = eq + 1;
    while (*v == ' ' || *v == '\t') v++;
    *value = atoi(v);

    return 1;
}

/* -----------------------------------------------------------------
 * Load config — supports V2 section-based format and auto-migrates old
 * ----------------------------------------------------------------- */
void load_config(void) {
    FILE *f = fopen(CONFIG_FILE, "r");
    if (!f) {
        LOG("Config: no %s found, using defaults", CONFIG_FILE);
        return;
    }

    int is_v2 = is_v2_format(f);
    char line[256];
    int loaded = 0;
    /* Single-pass section tracking: 0=none, 1=cheats, 2=favorites, 3=hotkeys.
     * V1 files have no sections — treat everything as cheats. */
    int section = is_v2 ? 0 : 1;

    while (fgets(line, sizeof(line), f)) {
        char *sp = line;
        while (*sp == ' ' || *sp == '\t') sp++;

        /* Section header — set current section, never consume extra lines */
        if (*sp == '[') {
            if (is_v2) {
                if (_strnicmp(sp, "[Cheats]", 8) == 0) section = 1;
                else if (_strnicmp(sp, "[Favorites]", 11) == 0) section = 2;
                else if (_strnicmp(sp, "[Hotkeys]", 9) == 0) section = 3;
                else section = 0;
            }
            continue;
        }

        /* Skip comments and blank lines */
        if (*sp == '#' || *sp == ';' || *sp == '\n' || *sp == '\r' || *sp == '\0')
            continue;

        /* Favorites section: favorite_<n>=<tab>,<item> */
        if (is_v2 && section == 2) {
            int tab = -1, item = -1;
            if (sscanf(sp, "favorite_%*d=%d,%d", &tab, &item) >= 2) {
                if (tab >= 0 && tab <= 11 && item >= 0 && item <= 63)
                    favorites_deserialize_add(tab, item);
            }
            continue;
        }

        /* Hotkeys section: hotkey_<n>=<vk>,<tab>,<item> (vk hex or decimal) */
        if (is_v2 && section == 3) {
            int vk = 0, tab = 0, item = 0;
            int ok = 0;
            if (sscanf(sp, "hotkey_%*d=0x%x,%d,%d", &vk, &tab, &item) >= 3) ok = 1;
            else if (sscanf(sp, "hotkey_%*d=%d,%d,%d", &vk, &tab, &item) >= 3) ok = 1;
            if (ok && vk >= 1 && vk <= 255 &&
                vk != 0x70 && vk != 0x71 && vk != 0x72 &&  /* F1/F2/F3 reserved */
                tab >= 0 && tab <= 11 && item >= 0 && item <= 63)
                hotkeys_bind(vk, tab, item);
            continue;
        }

        if (is_v2 && section != 1) continue;

        char name[64];
        int value;
        if (!parse_kv_line(line, name, sizeof(name), &value))
            continue;

        ConfigMapping *m = find_mapping(name);
        if (m) {
            *m->field = value;
            loaded++;
        }
    }
    fclose(f);

    LOG("Config: loaded %d settings from %s (v%d format)", loaded, CONFIG_FILE, is_v2 ? 2 : 1);

    /* Migrate old format: write V2 on next save */
    if (!is_v2 && loaded > 0) {
        LOG("Config: migrating old format to V2 on next save");
    }

    /* Initialise F3 key state tracking (defined in menu.c) */
}

/* -----------------------------------------------------------------
 * Save config in V2 section-based format
 * ----------------------------------------------------------------- */
void save_config(void) {
    FILE *f = fopen(CONFIG_FILE, "w");
    if (!f) {
        LOG("Config: failed to write %s", CONFIG_FILE);
        return;
    }

    fprintf(f, "# " MIGRATION_MARKER "\n");
    fprintf(f, "# Black Pearl Engine config v2\n");
    fprintf(f, "# Generated automatically. Edit and restart to apply.\n\n");

    /* --- [Cheats] section --- */
    fprintf(f, "[Cheats]\n");
    for (int i = 0; i < MAP_COUNT; i++) {
        fprintf(f, "%s=%d\n", g_map[i].name, *g_map[i].field);
    }
    fprintf(f, "\n");

    /* --- [Favorites] section --- */
    int fcount = favorites_count();
    if (fcount > 0) {
        fprintf(f, "[Favorites]\n");
        for (int i = 0; i < fcount; i++) {
            const FavoriteEntry *fe = favorites_get(i);
            if (fe)
                fprintf(f, "favorite_%d=%d,%d\n", i, fe->tab, fe->item);
        }
        fprintf(f, "\n");
    }

    /* --- [Hotkeys] section --- */
    int hk_count = 0;
    for (int i = 0; i < HOTKEYS_MAX; i++) {
        if (g_hotkeys[i].active) hk_count++;
    }
    if (hk_count > 0) {
        fprintf(f, "[Hotkeys]\n");
        int idx = 0;
        for (int i = 0; i < HOTKEYS_MAX; i++) {
            if (g_hotkeys[i].active) {
                fprintf(f, "hotkey_%d=0x%x,%d,%d\n", idx++,
                        g_hotkeys[i].vk,
                        g_hotkeys[i].tab,
                        g_hotkeys[i].item);
            }
        }
        fprintf(f, "\n");
    }

    fclose(f);
    LOG("Config: saved %d settings, %d favorites, %d hotkeys to %s",
        MAP_COUNT, fcount, hk_count, CONFIG_FILE);
}
