#include <cstddef>
#include "presets.h"
#include "cheats.h"
#include "config_loader.h"
#include "utils.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <direct.h>
#include <io.h>

/* ================================================================
 * Presets System Implementation
 *
 * Each preset is a flat key=value file (same format as the old
 * single-line config) stored as bpe_preset_<name>.cfg in the
 * game directory.
 * ================================================================ */

/* -----------------------------------------------------------------
 * Config mapping for serialisation (reuse the same field list as
 * the main config, but serialise independently to avoid circular
 * dependency on the full config loader).
 * ----------------------------------------------------------------- */
static const struct { const char *name; int offset; } s_preset_fields[] = {
    {"invincible",           offsetof(CheatsState, invincible)},
    {"infinite_studs",       offsetof(CheatsState, infinite_studs)},
    {"super_speed",          offsetof(CheatsState, super_speed)},
    {"super_jump",           offsetof(CheatsState, super_jump)},
    {"super_jump_scale",     offsetof(CheatsState, super_jump_scale)},
    {"speed_mult",           offsetof(CheatsState, speed_mult)},
    {"time_freeze",          offsetof(CheatsState, time_freeze)},
    {"noclip",               offsetof(CheatsState, noclip)},
    {"show_debug",           offsetof(CheatsState, show_debug)},
    {"show_fps",             offsetof(CheatsState, show_fps)},
    {"score_mult",           offsetof(CheatsState, score_mult)},
    {"force_custom_studs",   offsetof(CheatsState, force_custom_studs)},
    {"custom_stud_value",    offsetof(CheatsState, custom_stud_value)},
    {"force_golden_bricks",  offsetof(CheatsState, force_golden_bricks)},
    {"golden_brick_value",   offsetof(CheatsState, golden_brick_value)},
    {"uw_enabled",           offsetof(CheatsState, uw_enabled)},
    {"uw_ratio",             offsetof(CheatsState, uw_ratio)},
    {"char_scale",           offsetof(CheatsState, char_scale)},
    {"char_scale_val",       offsetof(CheatsState, char_scale_val)},
    {"remove_water",         offsetof(CheatsState, remove_water)},
    {"underwater_breath",    offsetof(CheatsState, underwater_breath)},
    {"reverse_damage",       offsetof(CheatsState, reverse_damage)},
    {"one_heart",            offsetof(CheatsState, one_heart)},
    {"no_knockback",         offsetof(CheatsState, no_knockback)},
    {"no_hit_reaction",      offsetof(CheatsState, no_hit_reaction)},
    {"damage_response_only", offsetof(CheatsState, damage_response_only)},
    {"one_hit_kill",         offsetof(CheatsState, one_hit_kill)},
    {"stud_magnet",          offsetof(CheatsState, stud_magnet)},
    {"quick_combo",          offsetof(CheatsState, quick_combo)},
    {"super_punch",          offsetof(CheatsState, super_punch)},
    {"always_gold",          offsetof(CheatsState, always_gold)},
    {"mega_destruct",        offsetof(CheatsState, mega_destruct)},
    {"infinite_cannonballs", offsetof(CheatsState, infinite_cannonballs)},
    {"free_camera",          offsetof(CheatsState, free_camera)},
    {"fov",                  offsetof(CheatsState, fov)},
    {"teleport_slot_0",      offsetof(CheatsState, teleport_slot_0)},
    {"teleport_slot_1",      offsetof(CheatsState, teleport_slot_1)},
    {"teleport_slot_2",      offsetof(CheatsState, teleport_slot_2)},
    {"teleport_slot_3",      offsetof(CheatsState, teleport_slot_3)},
    {"teleport_slot_4",      offsetof(CheatsState, teleport_slot_4)},
};
#define PRESET_FIELD_COUNT (sizeof(s_preset_fields) / sizeof(s_preset_fields[0]))

static int s_field_val(int idx) {
    return *(int*)((char*)&g_cheats + s_preset_fields[idx].offset);
}

static void s_field_set(int idx, int val) {
    *(int*)((char*)&g_cheats + s_preset_fields[idx].offset) = val;
}

/* -----------------------------------------------------------------
 * Helpers
 * ----------------------------------------------------------------- */
static int build_path(char *buf, size_t sz, const char *name) {
    if (!buf || sz == 0 || !name || !*name) return 0;
    if (strlen(name) >= PRESET_NAME_LEN) return 0;
    if (strstr(name, "..")) return 0;
    if (strpbrk(name, "/\\:")) return 0;
    char file[sizeof(PRESET_PREFIX) + PRESET_NAME_LEN + sizeof(PRESET_SUFFIX)];
    snprintf(file, sizeof(file), PRESET_PREFIX "%s" PRESET_SUFFIX, name);
    bpe_path(buf, sz, file);
    return 1;
}

static int file_exists(const char *path) {
    return access(path, 0) == 0;
}

/* -----------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------- */
void presets_init(void) {
    /* Directory-based presets — files live in the current directory.
     * Nothing to create up-front. */
    LOG("Presets: initialised");
}

int presets_save(const char *name) {
    char path[260];
    if (!build_path(path, sizeof(path), name)) {
        LOG("Presets: invalid preset name '%s'", name ? name : "(null)");
        return 0;
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        LOG("Presets: failed to write %s", path);
        return 0;
    }

    fprintf(f, "# Black Pearl Engine - Preset: %s\n", name);
    fprintf(f, "# " __DATE__ " " __TIME__ "\n\n");

    for (int i = 0; i < PRESET_FIELD_COUNT; i++) {
        fprintf(f, "%s=%d\n", s_preset_fields[i].name, s_field_val(i));
    }

    fclose(f);
    LOG("Presets: saved '%s' (%d fields)", name, PRESET_FIELD_COUNT);
    return 1;
}

int presets_load(const char *name) {
    char path[260];
    if (!build_path(path, sizeof(path), name)) {
        LOG("Presets: invalid preset name '%s'", name ? name : "(null)");
        return 0;
    }

    if (!file_exists(path)) {
        LOG("Presets: '%s' not found", name);
        return 0;
    }

    FILE *f = fopen(path, "r");
    if (!f) return 0;

    char line[256];
    int loaded = 0;
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == ';' || *p == '\n' || *p == '\r' || *p == '\0')
            continue;
        if (*p == '[') continue;

        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        char *name_field = p;
        char *val_str = eq + 1;

        while (*val_str == ' ' || *val_str == '\t') val_str++;
        char *end = val_str + strlen(val_str) - 1;
        while (end >= val_str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r'))
            *end-- = '\0';

        for (int i = 0; i < PRESET_FIELD_COUNT; i++) {
            if (strcmp(s_preset_fields[i].name, name_field) == 0) {
                s_field_set(i, atoi(val_str));
                loaded++;
                break;
            }
        }
    }
    fclose(f);

    LOG("Presets: loaded '%s' (%d fields)", name, loaded);
    return loaded > 0;
}

int presets_delete(const char *name) {
    char path[260];
    if (!build_path(path, sizeof(path), name)) {
        LOG("Presets: invalid preset name '%s'", name ? name : "(null)");
        return 0;
    }

    if (!file_exists(path)) {
        LOG("Presets: '%s' not found, nothing to delete", name);
        return 0;
    }

    if (remove(path) == 0) {
        LOG("Presets: deleted '%s'", name);
        return 1;
    }
    LOG("Presets: failed to delete '%s'", name);
    return 0;
}

int presets_list(char names[][PRESET_NAME_LEN], int max) {
    /* Scan the DLL directory for bpe_preset_*.cfg files */
    char dir[MAX_PATH], pattern[MAX_PATH + 32];
    bpe_path(dir, sizeof(dir), "");
    snprintf(pattern, sizeof(pattern), "%s" PRESET_PREFIX "*" PRESET_SUFFIX, dir);
    struct _finddata_t fd;
    intptr_t handle = _findfirst(pattern, &fd);
    if (handle == -1) return 0;

    int count = 0;
    do {
        if (count >= max) break;
        /* Extract name between prefix and suffix */
        const char *start = fd.name + strlen(PRESET_PREFIX);
        size_t name_len = strlen(start) - strlen(PRESET_SUFFIX);
        if (name_len > 0 && name_len < PRESET_NAME_LEN) {
            memcpy(names[count], start, name_len);
            names[count][name_len] = '\0';
            count++;
        }
    } while (_findnext(handle, &fd) == 0);

    _findclose(handle);
    return count;
}
