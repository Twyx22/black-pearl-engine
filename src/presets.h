#ifndef BPE_PRESETS_H
#define BPE_PRESETS_H

/* ================================================================
 * Presets System (T4.2)
 *
 * Save/load named snapshots of all cheat toggles.
 * Stored as separate files: bpe_preset_<name>.cfg
 * Max 20 preset slots.
 * ================================================================ */

#define PRESET_MAX_SLOTS   20
#define PRESET_NAME_LEN    48
#define PRESET_PREFIX      "bpe_preset_"
#define PRESET_SUFFIX      ".cfg"

/** Initialise the presets directory (create if missing). */
void presets_init(void);

/** Save current CheatsState as a named preset. Returns 1 on success. */
int presets_save(const char *name);

/** Load a named preset, applying all cheat values. Returns 1 on success. */
int presets_load(const char *name);

/** Delete a named preset. Returns 1 on success. */
int presets_delete(const char *name);

/** Enumerate saved presets into the names array.
 *  @return Number of presets found (capped at max). */
int presets_list(char names[][PRESET_NAME_LEN], int max);

#endif /* BPE_PRESETS_H */
