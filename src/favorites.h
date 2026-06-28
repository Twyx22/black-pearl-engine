#ifndef BPE_FAVORITES_H
#define BPE_FAVORITES_H

/* ================================================================
 * Favorites System (T4.3)
 *
 * Mark cheats as favourites so they appear first in each tab.
 * 32 max favourites. Toggle with a keybind (F3 while on a cheat).
 * ================================================================ */

#define FAVORITES_MAX 32

typedef struct {
    int tab;   /* Tab index */
    int item;  /* Item index within that tab */
} FavoriteEntry;

/** Initialise favourites from config. */
void favorites_init(void);

/** Check if a specific item is favourited. */
int favorites_is_favorite(int tab, int item);

/** Toggle the favourite status of an item. Returns 1 if now favourited. */
int favorites_toggle(int tab, int item);

/** Add a favourite. Returns 1 on success, 0 if full or already added. */
int favorites_add(int tab, int item);

/** Remove a favourite. Returns 1 on success. */
int favorites_remove(int tab, int item);

/** Remove by index in the favourites list. Returns 1 on success. */
int favorites_remove_by_index(int idx);

/** Get total number of favourites. */
int favorites_count(void);

/** Get a favourite entry by index. Returns NULL if out of range. */
const FavoriteEntry* favorites_get(int index);

/** Serialize favourites to/from config. */
void favorites_serialize(void);

/** Deserialize (append) a single favourite from config. */
void favorites_deserialize_add(int tab, int item);

#endif /* BPE_FAVORITES_H */
