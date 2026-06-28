#include "teleport.h"
#include "utils.h"
#include "config.h"
#include "config_loader.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ================================================================
 * Teleport Module
 *
 * Finds the player entity pointer via the game's entity name table
 * (at base + ENTITY_TABLE_OFFSET = 0x00C8F400), reads/writes
 * position floats at offsets +0x28 (X), +0x2C (Y), +0x30 (Z),
 * and manages 10 named save slots with disk persistence.
 * ================================================================ */

#define TELEPORT_FILE "bpe_teleport.cfg"

/* -----------------------------------------------------------------
 * Module state
 * ----------------------------------------------------------------- */

static TeleportSlot g_slots[TELEPORT_MAX_SLOTS];
static DWORD g_player_entity = 0;  /**< Pointer to player entity struct in game memory */
static int g_teleport_inited = 0;

/* -----------------------------------------------------------------
 * Player entity discovery
 *
 * The entity table at base + ENTITY_TABLE_OFFSET contains pointers
 * to entity structs.  Each entity struct's first field is an inline
 * name string.  Position data is at struct + 0x28/0x2C/0x30.
 * ----------------------------------------------------------------- */

/**
 * @brief Scan the entity table and locate the player entity pointer.
 *
 * Two-pass approach:
 *   Pass 1 — Try to match known player names (case-sensitive).
 *   Pass 2 — Use the first valid entity as a fallback.
 */
static void find_player_entity(void)
{
    DWORD base = (DWORD)GetModuleHandleA(NULL);
    if (!base) {
        LOG("Teleport: GetModuleHandleA failed");
        return;
    }

    DWORD *table = (DWORD *)(base + ENTITY_TABLE_OFFSET);

    /* Known player character names to search for */
    static const char *player_names[] = {
        "PLAYER", "Player", "player",
        "Jack", "jack", "JACK",
        "JackSparrow", "JACKSPARROW",
        "Will", "WILL", "will",
        "Elizabeth", "ELIZABETH",
        "TiaDalma", "DavyJones",
        "playable_character", "playable",
        "HeroPlayer", "hero_player",
        NULL
    };

    /* -- Pass 1: match by name -- */
    for (int i = 0; i < 1024; i++) {
        DWORD ptr = table[i];
        if (!ptr || IsBadReadPtr((void *)ptr, 4))
            continue;

        const char *name = (const char *)ptr;

        /* Quick validity — first char must be printable ASCII */
        if (name[0] < 0x20 || name[0] > 0x7E)
            continue;

        for (int n = 0; player_names[n]; n++) {
            if (strcmp(name, player_names[n]) == 0) {
                g_player_entity = ptr;
                LOG("Teleport: found player entity at 0x%08X ('%s')", ptr, name);
                return;
            }
        }
    }

    /* -- Pass 2: use first valid printable entity as fallback -- */
    for (int i = 0; i < 1024; i++) {
        DWORD ptr = table[i];
        if (!ptr || IsBadReadPtr((void *)ptr, 4))
            continue;

        const char *name = (const char *)ptr;
        if (name[0] < 0x20 || name[0] > 0x7E)
            continue;

        /* Verify we can read position floats from this entity */
        float test_x;
        if (safe_read(ptr + 0x28, &test_x, sizeof(float))) {
            g_player_entity = ptr;
            LOG("Teleport: using fallback entity at 0x%08X ('%s') — could not find by player name", ptr, name);
            return;
        }
    }

    LOG("Teleport: no valid player entity found in entity table (0x%08X)", base + ENTITY_TABLE_OFFSET);
}

/* -----------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------- */

void teleport_init(void)
{
    if (g_teleport_inited)
        return;
    g_teleport_inited = 1;

    /* Load saved slots from disk first */
    teleport_deserialize();

    /* Locate the player entity in game memory */
    find_player_entity();

    LOG("Teleport: initialized (%s entity at 0x%08X)",
        g_player_entity ? "found" : "NO",
        g_player_entity);
}

int teleport_get_current_pos(float *x, float *y, float *z)
{
    if (!g_player_entity || !x || !y || !z)
        return 0;

    if (!safe_read(g_player_entity + 0x28, x, sizeof(float)))
        return 0;
    if (!safe_read(g_player_entity + 0x2C, y, sizeof(float)))
        return 0;
    if (!safe_read(g_player_entity + 0x30, z, sizeof(float)))
        return 0;

    return 1;
}

int teleport_set_pos(float x, float y, float z)
{
    if (!g_player_entity)
        return 0;

    if (!safe_write(g_player_entity + 0x28, &x, sizeof(float)))
        return 0;
    if (!safe_write(g_player_entity + 0x2C, &y, sizeof(float)))
        return 0;
    if (!safe_write(g_player_entity + 0x30, &z, sizeof(float)))
        return 0;

    return 1;
}

void teleport_save(int slot)
{
    if (slot < 0 || slot >= TELEPORT_MAX_SLOTS)
        return;

    TeleportSlot *s = &g_slots[slot];
    if (teleport_get_current_pos(&s->x, &s->y, &s->z)) {
        s->active = 1;
        if (s->label[0] == '\0') {
            snprintf(s->label, sizeof(s->label), "Slot %d", slot);
        }
        LOG("Teleport: saved slot %d '%.31s' (%.2f, %.2f, %.2f)",
            slot, s->label, s->x, s->y, s->z);

        /* Persist immediately */
        teleport_serialize();
    }
}

void teleport_load(int slot)
{
    if (slot < 0 || slot >= TELEPORT_MAX_SLOTS)
        return;

    TeleportSlot *s = &g_slots[slot];
    if (!s->active) {
        LOG("Teleport: slot %d is empty", slot);
        return;
    }

    if (teleport_set_pos(s->x, s->y, s->z)) {
        LOG("Teleport: loaded slot %d '%.31s' to (%.2f, %.2f, %.2f)",
            slot, s->label, s->x, s->y, s->z);
    }
}

const TeleportSlot *teleport_get_slot(int slot)
{
    if (slot < 0 || slot >= TELEPORT_MAX_SLOTS)
        return NULL;
    return &g_slots[slot];
}

void teleport_set_label(int slot, const char *label)
{
    if (slot < 0 || slot >= TELEPORT_MAX_SLOTS || !label)
        return;
    strncpy(g_slots[slot].label, label, sizeof(g_slots[slot].label) - 1);
    g_slots[slot].label[sizeof(g_slots[slot].label) - 1] = '\0';
}

/* -----------------------------------------------------------------
 * Serialization — persist slot data to bpe_teleport.cfg
 * ----------------------------------------------------------------- */

void teleport_serialize(void)
{
    FILE *f = fopen(TELEPORT_FILE, "w");
    if (!f) {
        LOG("Teleport: cannot write %s", TELEPORT_FILE);
        return;
    }

    fprintf(f, "# Black Pearl Engine — Teleport save slots\n");
    fprintf(f, "# Generated automatically. Edit to restore saved positions.\n");
    fprintf(f, "# Format: teleport_<slot>_<field>=<value>\n\n");

    for (int i = 0; i < TELEPORT_MAX_SLOTS; i++) {
        TeleportSlot *s = &g_slots[i];
        fprintf(f, "teleport_%d_active=%d\n",   i, s->active);
        fprintf(f, "teleport_%d_x=%.6f\n",      i, s->x);
        fprintf(f, "teleport_%d_y=%.6f\n",      i, s->y);
        fprintf(f, "teleport_%d_z=%.6f\n",      i, s->z);
        /* Sanitize label: only printable ASCII */
        char clean_label[sizeof(s->label)];
        for (size_t j = 0; j < sizeof(s->label); j++) {
            char c = s->label[j];
            clean_label[j] = (c >= 0x20 && c <= 0x7E) ? c : '\0';
            if (c == '\0') break;
        }
        clean_label[sizeof(clean_label) - 1] = '\0';
        fprintf(f, "teleport_%d_label=%s\n", i, clean_label);
        fprintf(f, "\n");
    }

    fclose(f);
    LOG("Teleport: saved %d slot(s) to %s", TELEPORT_MAX_SLOTS, TELEPORT_FILE);
}

void teleport_deserialize(void)
{
    FILE *f = fopen(TELEPORT_FILE, "r");
    if (!f) {
        /* First run — no teleport file yet, that's fine */
        return;
    }

    int count = 0;
    char line[256];

    while (fgets(line, sizeof(line), f)) {
        char *p = line;

        /* Skip leading whitespace */
        while (*p == ' ' || *p == '\t') p++;

        /* Skip comments, section headers, blank lines */
        if (*p == '#' || *p == ';' || *p == '[' || *p == '\n' || *p == '\r' || *p == '\0')
            continue;

        char *eq = strchr(p, '=');
        if (!eq) continue;

        *eq = '\0';
        char *name = p;
        char *val  = eq + 1;

        /* Trim trailing whitespace from name */
        char *end = name + strlen(name) - 1;
        while (end >= name && (*end == ' ' || *end == '\t')) *end-- = '\0';

        /* Trim leading whitespace from value */
        while (*val == ' ' || *val == '\t') val++;

        /* Trim trailing whitespace/newlines from value */
        end = val + strlen(val) - 1;
        while (end >= val && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r'))
            *end-- = '\0';

        /* Parse key: teleport_<slot>_<field> */
        int s;
        if (sscanf(name, "teleport_%d_active", &s) == 1 && s >= 0 && s < TELEPORT_MAX_SLOTS) {
            g_slots[s].active = atoi(val);
            count++;
        } else if (sscanf(name, "teleport_%d_x", &s) == 1 && s >= 0 && s < TELEPORT_MAX_SLOTS) {
            g_slots[s].x = (float)atof(val);
        } else if (sscanf(name, "teleport_%d_y", &s) == 1 && s >= 0 && s < TELEPORT_MAX_SLOTS) {
            g_slots[s].y = (float)atof(val);
        } else if (sscanf(name, "teleport_%d_z", &s) == 1 && s >= 0 && s < TELEPORT_MAX_SLOTS) {
            g_slots[s].z = (float)atof(val);
        } else if (sscanf(name, "teleport_%d_label", &s) == 1 && s >= 0 && s < TELEPORT_MAX_SLOTS) {
            strncpy(g_slots[s].label, val, sizeof(g_slots[s].label) - 1);
            g_slots[s].label[sizeof(g_slots[s].label) - 1] = '\0';
        }
    }

    fclose(f);

    /* Count actually-active slots */
    int active = 0;
    for (int i = 0; i < TELEPORT_MAX_SLOTS; i++) {
        if (g_slots[i].active) active++;
    }
    if (active > 0 || count > 0) {
        LOG("Teleport: loaded %d active slot(s) from %s", active, TELEPORT_FILE);
    }
}
