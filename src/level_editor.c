#include "level_editor.h"
#include "utils.h"
#include "config.h"
#include <stdio.h>
#include <string.h>

/* ================================================================
 * Constants
 * ================================================================ */

/* LevelEditor class constructor address (from Ghidra).
 * At _LEGOPirates.exe+0x18EB50 if base-relative, or absolute
 * 0x58EB50. The offset is relative to module base. */
#define LEVEL_EDITOR_CTOR_ABSOLUTE  0x0058EB50

/* RTTI strings (for locating the singleton manager) */
#define RTTI_LEVELEDITOR        ".?AVLevelEditor@@"
#define RTTI_MANAGER_ACCESSOR   ".?AUClassManagerAccessor<LevelEditor>@@"

/* ================================================================
 * State
 * ================================================================ */
static int  g_active = 0;          /* 1 = editor believed to be active */
static int  g_launch_attempted = 0; /* 1 = we tried to launch already */
static DWORD g_module_base = 0;    /* cached module base */
static DWORD g_level_editor_instance = 0; /* pointer to LevelEditor singleton */

/* ================================================================
 * Helpers
 * ================================================================ */

/* Get the module base address (cached) */
static DWORD module_base(void) {
    if (!g_module_base)
        g_module_base = (DWORD)GetModuleHandleA(NULL);
    return g_module_base;
}

/* Resolve a constructor address.
 * config.h defines LEVEL_EDITOR_CTOR_OFFSET as the offset from base
 * (Ghidra gave 0x58EB50, offset = 0x58EB50 - 0x400000 = 0x18EB50). */
static DWORD ctor_address(void) {
#ifdef LEVEL_EDITOR_CTOR
    return LEVEL_EDITOR_CTOR;     /* absolute address if defined */
#elif defined(LEVEL_EDITOR_CTOR_OFFSET)
    return module_base() + LEVEL_EDITOR_CTOR_OFFSET;
#else
    return LEVEL_EDITOR_CTOR_ABSOLUTE;
#endif
}

/* ================================================================
 * Try to find the LevelEditor singleton instance.
 *
 * Strategy:
 *   1. Look for RTTI string ".?AVLevelEditor@@" in .rdata
 *   2. Find cross-references to it (references from the vtables)
 *   3. Walk backwards to find the constructor / manager creation
 *   4. The ClassManagerAccessor<LevelEditor> stores the singleton
 *      pointer; we scan for it in .data using the RTTI as anchor.
 *
 * For now we use a simpler heuristic: the LevelEditor is a singleton
 * whose instance pointer is stored at a known offset from a static
 * "Manager" object. We scan the .data section for pointers to the
 * constructor or RTTI.
 * ================================================================ */
static int find_level_editor_instance(void) {
    DWORD base = module_base();
    if (!base) return 0;

    /* 1. Locate the RTTI CompleteObjectLocator for LevelEditor in .rdata.
     *    We search for the RTTI string first. */
    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)base;
    PIMAGE_NT_HEADERS nt  = (PIMAGE_NT_HEADERS)(base + dos->e_lfanew);

    /* Find .rdata section for string scan */
    PIMAGE_SECTION_HEADER sec = IMAGE_FIRST_SECTION(nt);
    DWORD rdata_start = 0, rdata_end = 0;

    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        char sn[9] = {0};
        memcpy(sn, sec[i].Name, 8);
        if (strcmp(sn, ".rdata") == 0) {
            rdata_start = base + sec[i].VirtualAddress;
            rdata_end   = rdata_start + sec[i].Misc.VirtualSize;
            break;
        }
    }

    if (!rdata_start) return 0;

    /* Search for RTTI string in .rdata */
    DWORD rtti_addr = 0;
    const char *needle = RTTI_LEVELEDITOR;
    size_t needle_len = strlen(needle) + 1;

    for (DWORD a = rdata_start; a + needle_len <= rdata_end; a++) {
        unsigned char buf[256];
        size_t to_read = needle_len < sizeof(buf) ? needle_len : sizeof(buf);
        if (safe_read(a, buf, to_read)) {
            if (memcmp(buf, needle, needle_len) == 0) {
                rtti_addr = a;
                break;
            }
        }
    }

    if (!rtti_addr) {
        LOG("Level Editor: RTTI string not found in .rdata");
        return 0;
    }

    LOG("Level Editor: RTTI string found at 0x%08X", rtti_addr);

    /* 2. Found the RTTI — the instance is typically in .data.
     *    We search for the constructor address in .data as a hint.
     *    This is a heuristic — in practice the singleton is stored
     *    at a fixed offset determined by static analysis. */
    DWORD ctor = ctor_address();
    LOG("Level Editor: constructor at 0x%08X (base+offset)", ctor);

    /* For now, we can't reliably locate the singleton without deeper
     * RE of the ClassManagerAccessor template. We flag that we found
     * the RTTI and will try to call the constructor directly. */
    return 0;  /* instance not yet located */
}

/* ================================================================
 * Try to call the LevelEditor constructor via a function call.
 *
 * We attempt to locate the LevelEditor manager's creation point
 * by finding a CALL to the constructor address in .text and then
 * calling that wrapper function, which should create the singleton.
 * ================================================================ */
static int call_constructor(void) {
    DWORD ctor = ctor_address();

    /* Read the bytes at the constructor address to ensure it's code */
    unsigned char probe[4];
    if (!safe_read(ctor, probe, 4)) {
        LOG("Level Editor: constructor address 0x%08X is not readable", ctor);
        return 0;
    }

    /* We define the constructor as a __thiscall function that takes
     * a 'this' pointer and returns void. Since LevelEditor is a
     * singleton, we need to find or allocate the 'this' pointer.
     *
     * In the game, the LevelEditor is created via
     *   ClassManagerAccessor<LevelEditor>::get()
     * which lazily constructs the singleton.
     *
     * We attempt to scan for this manager accessor in .text. */

    DWORD text_start = 0, text_size = 0;
    if (!find_text_section(&text_start, &text_size)) {
        LOG("Level Editor: cannot find .text section");
        return 0;
    }

    /* Look for call pattern: push 0; push 0; push ctor; call ??? */
    unsigned char ctor_bytes[4];
    ctor_bytes[0] = (unsigned char)(ctor & 0xFF);
    ctor_bytes[1] = (unsigned char)((ctor >> 8) & 0xFF);
    ctor_bytes[2] = (unsigned char)((ctor >> 16) & 0xFF);
    ctor_bytes[3] = (unsigned char)((ctor >> 24) & 0xFF);

    /* Search for "68 <ctor>" (PUSH ctor) pattern */
    DWORD found = 0;
    for (DWORD a = text_start; a + 5 <= text_start + text_size; a++) {
        unsigned char buf[5];
        if (safe_read(a, buf, 5)) {
            if (buf[0] == 0x68 && memcmp(buf + 1, ctor_bytes, 4) == 0) {
                /* Found a push of the constructor address — this is likely
                 * in a function that creates LevelEditor */
                LOG("Level Editor: found constructor reference at 0x%08X (PUSH ctor)", a);
                found = a;
                break;
            }
        }
    }

    if (!found) {
        LOG("Level Editor: could not locate constructor reference in .text");
        return 0;
    }

    /* Find the function containing this reference and call it */
    /* For safety, we use CreateRemoteThread to call the enclosing function.
     * This is a standard LEGO game pattern for instantiating singletons. */
    LOG("Level Editor: constructor reference found — would call enclosing function");
    return 0;  /* placeholder — real invocation via CreateRemoteThread */
}

/* ================================================================
 * Public API
 * ================================================================ */

void level_editor_launch(void) {
    if (g_active) {
        LOG("Level Editor: already active");
        return;
    }

    LOG("Level Editor: attempting to launch...");

    if (g_launch_attempted) {
        LOG("Level Editor: already attempted this session");
        return;
    }
    g_launch_attempted = 1;

    /* Strategy 1: Find the LevelEditor singleton instance via RTTI */
    if (find_level_editor_instance()) {
        LOG("Level Editor: found singleton instance");
        g_active = 1;
        return;
    }

    /* Strategy 2: Call the constructor via the wrapper function */
    if (call_constructor()) {
        g_active = 1;
        return;
    }

    /* Strategy 3: Direct constructor call (__thiscall) — last resort */
    /* We need a 'this' pointer. Since LevelEditor is a singleton,
     * we try to allocate one via malloc and call the constructor on it. */
    DWORD ctor = ctor_address();
    DWORD base = module_base();

    LOG("Level Editor: trying direct constructor invocation");
    LOG("Level Editor: ctor=0x%08X, base=0x%08X", ctor, base);

    /* The LevelEditor singleton typically stores one vtable pointer
     * size of memory.  Allocate on the game heap if possible, or
     * just use a static buffer. */
    static unsigned char instance_buf[256];  /* hopefully enough */
    memset(instance_buf, 0, sizeof(instance_buf));

    /* Construct via __thiscall: ECX = this */
    /* We use a small assembly trampoline:
     *   mov ecx, instance_buf
     *   call ctor
     *   ret
     */
    /* Since we can't easily inline assembly in MinGW, we try to
     * use the built-in game function that creates the LevelEditor
     * via the ClassManagerAccessor pattern.
     *
     * For now we log the intent and return false — full implementation
     * requires either:
     *   a) Exact knowledge of the manager singleton pointer
     *   b) A small asm trampoline */
    LOG("Level Editor: direct invocation not yet implemented — see RE docs");
    LOG("Level Editor: known address = 0x%08X (ctor)", ctor);
    LOG("Level Editor: RTTI string at 0x%08X (config.h)", LEVEL_EDITOR_RTTI_STR);
    LOG("Level Editor: RTTI string at 0x%08X (config.h)", LEVEL_EDITOR_CLASS_STR);

    g_active = 0;
}

int level_editor_is_active(void) {
    return g_active;
}

/* Additional helpers */

/* Try to find the manager accessor by scanning for the RTTI string
 * reference in .rdata and then looking for pointers to it in .data */
static int find_manager_via_data_scan(void) {
    DWORD base = module_base();
    if (!base) return 0;

    /* Build the RTTI manager string reference */
    /* The LevelEditor is managed by ClassManagerAccessor<LevelEditor>,
     * whose RTTI is at LEVELEDITOR_MANAGER_RTTI in config.h */
    DWORD mgr_rtti = LEVELEDITOR_MANAGER_RTTI;

    /* Try scanning .data for DWORD 4-byte aligned values close to this RTTI */
    LOG("Level Editor: manager RTTI at 0x%08X (config.h)", mgr_rtti);
    return 0;
}

void level_editor_init(void) {
    LOG("Level Editor: module initialized");
    LOG("Level Editor: ctor at base+0x%X = 0x%08X",
        LEVEL_EDITOR_CTOR_OFFSET, ctor_address());
    LOG("Level Editor: RTTI='%s', Manager RTTI=0x%08X",
        RTTI_LEVELEDITOR, LEVELEDITOR_MANAGER_RTTI);

    /* Pre-cache module base */
    module_base();
}
