#include "favorites.h"
#include "utils.h"
#include <string.h>

/* ================================================================
 * Favorites System Implementation
 *
 * Stores up to FAVORITES_MAX (tab, item) pairs in a flat array.
 * Serialised to the config file under a [Favorites] section as:
 *   favorite_0=tab,item
 *   favorite_1=tab,item
 * ================================================================ */

static FavoriteEntry s_favorites[FAVORITES_MAX];
static int s_count = 0;

/* -----------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------- */
void favorites_init(void) {
    s_count = 0;
    memset(s_favorites, 0, sizeof(s_favorites));
    /* Deserialisation is handled by config_loader scanning [Favorites] */
}

int favorites_is_favorite(int tab, int item) {
    for (int i = 0; i < s_count; i++) {
        if (s_favorites[i].tab == tab && s_favorites[i].item == item)
            return 1;
    }
    return 0;
}

int favorites_toggle(int tab, int item) {
    /* Remove if already favourited */
    for (int i = 0; i < s_count; i++) {
        if (s_favorites[i].tab == tab && s_favorites[i].item == item) {
            /* Shift remaining entries down */
            if (i < s_count - 1)
                memmove(&s_favorites[i], &s_favorites[i + 1],
                        (s_count - i - 1) * sizeof(FavoriteEntry));
            s_count--;
            LOG("Favorites: removed (tab=%d, item=%d)", tab, item);
            return 0;
        }
    }
    /* Add new favourite */
    if (s_count >= FAVORITES_MAX) {
        LOG("Favorites: max reached (%d)", FAVORITES_MAX);
        return 0;
    }
    s_favorites[s_count].tab = tab;
    s_favorites[s_count].item = item;
    s_count++;
    LOG("Favorites: added (tab=%d, item=%d) [%d total]", tab, item, s_count);
    return 1;
}

int favorites_add(int tab, int item) {
    if (s_count >= FAVORITES_MAX) return 0;
    for (int i = 0; i < s_count; i++) {
        if (s_favorites[i].tab == tab && s_favorites[i].item == item)
            return 0;  /* Already exists */
    }
    s_favorites[s_count].tab = tab;
    s_favorites[s_count].item = item;
    s_count++;
    return 1;
}

int favorites_remove(int tab, int item) {
    for (int i = 0; i < s_count; i++) {
        if (s_favorites[i].tab == tab && s_favorites[i].item == item) {
            if (i < s_count - 1)
                memmove(&s_favorites[i], &s_favorites[i + 1],
                        (s_count - i - 1) * sizeof(FavoriteEntry));
            s_count--;
            return 1;
        }
    }
    return 0;
}

int favorites_remove_by_index(int idx) {
    if (idx < 0 || idx >= s_count) return 0;
    if (idx < s_count - 1)
        memmove(&s_favorites[idx], &s_favorites[idx + 1],
                (s_count - idx - 1) * sizeof(FavoriteEntry));
    s_count--;
    return 1;
}

int favorites_count(void) {
    return s_count;
}

const FavoriteEntry* favorites_get(int index) {
    if (index < 0 || index >= s_count) return NULL;
    return &s_favorites[index];
}

void favorites_serialize(void) {
    /* Stored in config_loader.c — this is called during save_config */
}

void favorites_deserialize_add(int tab, int item) {
    if (s_count < FAVORITES_MAX) {
        s_favorites[s_count].tab = tab;
        s_favorites[s_count].item = item;
        s_count++;
    }
}
