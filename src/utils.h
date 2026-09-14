#ifndef BPE_UTILS_H
#define BPE_UTILS_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

#define MOD_NAME "Black Pearl Engine"
#define MOD_VER  "v5.1"

/** Suppress unused parameter warnings without affecting codegen. */
#define BPE_UNUSED(x) ((void)(x))

extern HINSTANCE g_hinst;

/* ------------------------------------------------------------------ */
/*  Logging                                                            */
/* ------------------------------------------------------------------ */

/** Initialise the debug console and logging subsystem. */
void log_init(void);

/** Write a timestamped message to both bpe.log and the debug console. */
void LOG(const char *fmt, ...);

/* ------------------------------------------------------------------ */
/*  Memory patching (low-level helpers)                                */
/* ------------------------------------------------------------------ */

/** Apply len bytes of data at addr, changing page protection to RW. */
void patch_mem(DWORD addr, const void *data, size_t len);

/* ------------------------------------------------------------------ */
/*  .text section discovery                                            */
/* ------------------------------------------------------------------ */

/**
 * @brief Locate the .text section of the current module.
 *
 * @param out_start Receives the virtual address of the section start.
 * @param out_size  Receives the virtual size of the section.
 * @return 1 on success, 0 on failure.
 */
int find_text_section(DWORD *out_start, DWORD *out_size);

/* ------------------------------------------------------------------ */
/*  Safe read / write with page-protection management                  */
/* ------------------------------------------------------------------ */

/**
 * @brief Write data to an arbitrary address, temporarily making the page
 *        writable.
 * @param addr  Target address.
 * @param data  Source buffer.
 * @param len   Number of bytes to write.
 * @return 1 on success, 0 on failure (logs the error internally).
 */
int safe_write(DWORD addr, const void *data, size_t len);

/**
 * @brief Read data from an arbitrary address, temporarily making the page
 *        readable (code pages are normally readable, but this ensures it).
 * @param addr Source address.
 * @param out  Destination buffer.
 * @param len  Number of bytes to read.
 * @return 1 on success, 0 on failure.
 */
int safe_read(DWORD addr, void *out, size_t len);

/* ------------------------------------------------------------------ */
/*  PatchRecord – save-apply-restore lifecycle                         */
/* ------------------------------------------------------------------ */

/**
 * @brief Describes a single binary patch with save/restore capability.
 *
 * Caller initialises to {0} (or zero-initialises the struct).  After a
 * successful patch_apply() the @a orig_bytes member is heap-allocated
 * and may be freed with patch_free() or restored with patch_restore().
 */
typedef struct {
    DWORD addr;               /**< Address of the patch site. */
    unsigned char *orig_bytes; /**< Heap-allocated copy of original bytes
                                    (NULL when no snapshot is held). */
    size_t size;              /**< Number of bytes saved / patched. */
    int active;               /**< 1 = patch currently applied. */
} PatchRecord;

/**
 * @brief Apply a patch, saving the original bytes first.
 *
 * Does nothing if the record is already active.
 *
 * @param pr        Pointer to an initialised PatchRecord.
 * @param new_bytes Buffer containing the replacement bytes (must be at
 *                  least pr->size bytes).
 * @return 1 on success, 0 on failure (logs internally).
 */
int patch_apply(PatchRecord *pr, const void *new_bytes);

/**
 * @brief Restore the original bytes that were saved by patch_apply().
 *
 * Frees the internal heap buffer and clears the active flag.
 * Does nothing if the record is not active.
 *
 * @param pr Pointer to an active PatchRecord.
 * @return 1 on success, 0 on failure.
 */
int patch_restore(PatchRecord *pr);

/**
 * @brief Free the snapshot buffer without touching the target memory.
 *
 * Safe to call on a zero-initialised or already-freed record.
 * Leaves the record in a clean {0} state.
 *
 * @param pr Pointer to a PatchRecord.
 */
void patch_free(PatchRecord *pr);

/* ------------------------------------------------------------------ */
/*  AOB (Array-of-Bytes) pattern scanning                              */
/* ------------------------------------------------------------------ */

/** Locate a byte pattern in the .text section.
 *
 * @param pattern    Byte values to match (wildcards are '?' in mask).
 * @param mask       Per-byte specifier: 'x' = must match, '?' = wildcard.
 * @param pattern_len Number of bytes to scan (uses strlen(mask) when
 *                   this is larger than mask length).
 * @return Virtual address of the first match, or 0 on failure.
 */
DWORD find_pattern(const unsigned char *pattern, const char *mask,
                   size_t pattern_len);

/* ------------------------------------------------------------------ */
/*  Cached AOB scanner                                                 */
/* ------------------------------------------------------------------ */

/**
 * @brief Result cache entry for find_pattern_cached().
 *
 * Zero-initialise before first use.  The @a pattern, @a mask and @a len
 * fields must stay stable across calls (they are not copied).
 */
typedef struct {
    DWORD result;               /**< Cached address (0 = not found). */
    const unsigned char *pattern; /**< Pointer to the byte pattern. */
    const char *mask;            /**< Pointer to the mask string. */
    size_t len;                  /**< Pattern length. */
    int scanned;                 /**< 1 = scan has been performed. */
} AobCache;

/**
 * @brief Like find_pattern() but caches the result so repeated calls
 *        with the same cache entry are nearly free.
 *
 * @param cache  Pointer to an AobCache (zero-init before first call).
 * @return The virtual address of the match, or 0.
 */
DWORD find_pattern_cached(AobCache *cache);

void bpe_path(char *out, size_t sz, const char *file);

#endif /* BPE_UTILS_H */
