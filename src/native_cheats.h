#ifndef BPE_NATIVE_CHEATS_H
#define BPE_NATIVE_CHEATS_H

/**
 * @file native_cheats.h
 * @brief Native cheat activation system for LEGO Pirates of the Caribbean.
 *
 * Activates the game's built-in cheats by hooking the internal
 * case-insensitive string comparison function (sub_473770 at base+0x73770).
 *
 * When a cheat is force-enabled via native_cheat_set(), the hook intercepts
 * calls to strcmpi and returns 0 ("equal") whenever the targeted cheat
 * name is compared, causing the game to activate the cheat internally.
 *
 * For deactivation, we cache flag addresses discovered via AOB scanning
 * or static RE; when the flag address is known, we can toggle it directly.
 * Unknown flags remain force-pending until discovered.
 */

/**
 * @brief Initialise the native cheat subsystem.
 *
 * Resolves the module base and installs the strcmpi hook via MinHook.
 * Safe to call multiple times (idempotent).
 */
void native_cheats_init(void);

/**
 * @brief Activate or deactivate a native cheat by its Squirrel identifier.
 *
 * @param cheat_name  The cheat identifier (e.g. "cheat_stud_magnet").
 * @param enable      1 = enable, 0 = disable.
 * @return 1 on success, 0 if the cheat string was not recognised.
 */
int  native_cheat_set(const char *cheat_name, int enable);

/**
 * @brief Check whether a native cheat is currently active.
 *
 * Reads the cached flag byte if known; otherwise returns the
 * force-pending state.
 *
 * @param cheat_name  The cheat identifier.
 * @return 1 if active, 0 if inactive or unknown.
 */
int  native_cheat_get(const char *cheat_name);

/**
 * @brief Attempt to discover cheat flag addresses by scanning .text for
 *        references to cheat strings.
 *
 * Intended for development / debug use; may be called from the menu or
 * console.  Results are cached in the internal flag table.
 */
void native_cheats_discover_flags(void);

/* ------------------------------------------------------------------ */
/*  Convenience wrappers for known native cheats                       */
/* ------------------------------------------------------------------ */

void native_stud_magnet(int enable);
void native_score_multiplier(int multiplier);   /* 0=off, 2,4,6,8,10 */
void native_always_score_multiply(int enable);
void native_invincibility(int enable);
void native_extra_hearts(int enable);
void native_breathe_underwater(int enable);
void native_fast_build(int enable);
void native_fast_fix(int enable);
void native_fast_dig(int enable);
void native_regenerate_hearts(int enable);
void native_disguises(int enable);
void native_character_studs(int enable);
void native_minikit_detector(int enable);
void native_powerbrick_detector(int enable);
void native_doomed_recovery(int enable);
void native_extra_toggle(int enable);

#endif /* BPE_NATIVE_CHEATS_H */
