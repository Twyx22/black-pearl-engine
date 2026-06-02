#include "config_loader.h"
#include "cheats.h"
#include "utils.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
    const char *name;
    int *field;
} ConfigMapping;

static ConfigMapping g_map[] = {
    {"infinite_studs",      &g_cheats.infinite_studs},
    {"invincible",          &g_cheats.invincible},
    {"super_speed",         &g_cheats.super_speed},
    {"super_jump",          &g_cheats.super_jump},
    {"moon_jump",           &g_cheats.moon_jump},
    {"time_freeze",         &g_cheats.time_freeze},
    {"noclip",              &g_cheats.noclip},
    {"show_debug",          &g_cheats.show_debug},
    {"show_fps",            &g_cheats.show_fps},
    {"score_mult",          &g_cheats.score_mult},
    {"force_custom_studs",  &g_cheats.force_custom_studs},
    {"custom_stud_value",   &g_cheats.custom_stud_value},
    {"force_golden_bricks", &g_cheats.force_golden_bricks},
    {"golden_brick_value",  &g_cheats.golden_brick_value},
};

#define MAP_COUNT (sizeof(g_map) / sizeof(g_map[0]))

static ConfigMapping *find_mapping(const char *name) {
    for (int i = 0; i < MAP_COUNT; i++) {
        if (strcmp(g_map[i].name, name) == 0)
            return &g_map[i];
    }
    return NULL;
}

void load_config(void) {
    FILE *f = fopen(CONFIG_FILE, "r");
    if (!f) {
        LOG("Config: no %s found, using defaults", CONFIG_FILE);
        return;
    }

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
        char *name = p;
        char *val_str = eq + 1;

        while (name < eq && (*(eq - 1) == ' ' || *(eq - 1) == '\t')) *(eq - 1) = '\0', eq--;
        while (*val_str == ' ' || *val_str == '\t') val_str++;
        char *end = val_str + strlen(val_str) - 1;
        while (end >= val_str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r'))
            *end-- = '\0';

        ConfigMapping *m = find_mapping(name);
        if (m) {
            *m->field = atoi(val_str);
            loaded++;
        }
    }
    fclose(f);
    LOG("Config: loaded %d settings from %s", loaded, CONFIG_FILE);
}

void save_config(void) {
    FILE *f = fopen(CONFIG_FILE, "w");
    if (!f) {
        LOG("Config: failed to write %s", CONFIG_FILE);
        return;
    }

    fprintf(f, "# Black Pearl Engine config\n");
    fprintf(f, "# Generated automatically. Edit and restart to apply.\n\n");

    for (int i = 0; i < MAP_COUNT; i++) {
        fprintf(f, "%s=%d\n", g_map[i].name, *g_map[i].field);
    }

    fclose(f);
    LOG("Config: saved %d settings to %s", MAP_COUNT, CONFIG_FILE);
}
