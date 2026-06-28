# Black Pearl Engine — v5 Implementation Plan

> **Generated:** 2026-06-28  
> **Project:** Black Pearl Engine — d3d9 proxy mod menu for LEGO Pirates of the Caribbean  
> **Current:** v4.1 (20 cheats, 6 tabs)  
> **Target:** v5.0 (35+ cheats, 10+ tabs, refactored codebase)  
> **IDA Session:** `721a737f` (LEGOPirates.exe, 23,837 functions)

---

## 0. IDA Discovery Summary

### 0.1 Native Cheat System (Game Built-in)

IDA revealed **91 native cheat strings** in the game binary. Many P0/P1 cheats can activate existing game functionality rather than binary-patching:

| String | Address | Purpose |
|--------|---------|---------|
| `cheat_stud_magnet` / `CHEAT_STUD_MAGNET` | 0xa6ee10 / 0xbff6d4 | **Stud Magnet** (P0) |
| `cheat_scorex2/4/6/8/10` / `CHEAT_SCOREX*` | 0xa6ed18-0xa6ed58 / 0xbff48c-0xbff570 | **Score Multiplier** (P0) |
| `cheat_always_score_multiply` / `CHEAT_ALWAYS_SCORE_MULTIPLY` | 0xa6ed7c / 0xbff3e0 | **Always Gold** (P1) |
| `CHEAT_SUPERSLAP` | 0xbff6fc | **Super Punch** (P1) potential |
| `CHEAT_INFINITE_TORPEDOS` | 0xbff49c | **Infinite Cannoballs** (P1) model |
| `CHEAT_FASTBUILD` | 0xbff550 | **Quick Combo** (P1) potential |
| `CHEAT_SELFDESTRUCT` | 0xbf89ec | **Mega Destruct** (P1) potential |
| `CHEAT_FASTFIX` | 0xbff46c | Repair speed |
| `CHEAT_FASTDIG` | 0xbff47c | Dig speed |
| `CHEAT_EXPLODINGBLASTERBOLTS` | 0xbff674 | Destructive projectiles |
| `CHEAT_SUPERBLASTERS` | 0xbff5cc | Stronger weapons |
| `CHEAT_ROCKETS` | 0xbff4dc | Rocket ammo |
| `CHEAT_DISGUISES` | 0xbff850 | Disguise toggle |
| `CHEAT_EXTRATOGGLE` | 0xbff86c | Extra features toggle |

The cheat name pointer table starts at **0xc8f480**. The activation function needs IDA analysis (T1).

### 0.2 Key Memory Addresses Found

| Purpose | Address | Notes |
|---------|---------|-------|
| `no_character_collisions` string | 0xbe0a38 | Ref'd by `sub_6AC280` (size 0x1852) |
| `SetAmmo` string | 0xc00328 | Function identifier for ammo setting |
| `max_ammo` string | 0xbedc7c | Variable name for max ammo |
| `easy_combos` string | 0xbf1ae4 | Quick Combo flag |
| `punch_always_stun` string | 0xbe333c | Super Punch flag |
| `LevelEditor` class name | 0xbc108c | For Level Editor Integration |

---

## 1. Dependency Graph (DAG)

### ASCII Dependency Diagram

```
PHASE 0: FOUNDATION
  T0.1 (utils refactor) ──────────────────────────────────────┐
  T0.2 (config.h expansion) ──────────────────────────────────┤
  T0.3 (build system) ────────────────────────────────────────┤
                                                               ▼
PHASE 1: IDA ANALYSIS ──────────────────────────────────► T1 (all IDA probes)
                                                               │
                                                               ▼
PHASE 2: CORE INFRASTRUCTURE ────────────────────────────► T2.1 (safe_patch) ◄── from T0.1
                                                          T2.2 (AOB cache)    ◄── from T0.1
                                                          T2.3 (cheat system) ◄── from T1
                                                               │
                          ┌────────────────────────────────────┼────────────────────────────┐
                          ▼                                    ▼                            ▼
PHASE 3: CHEAT MODULES    T3.1 (NoClip) ◄── T1                 T3.8 (Infinite Ammo) ◄── T1
(ALL PARALLEL)            T3.2 (Stud Magnet) ◄── T1            T3.9 (Super Punch) ◄── T1
                          T3.3 (Score Mult) ◄── T1             T3.10 (Always Gold) ◄── T1
                          T3.4 (Memory Browser) ◄── T0.1       T3.11 (Quick Combo) ◄── T1
                          T3.5 (Free Camera) ◄── T1            T3.12 (Mega Destruct) ◄── T1
                          T3.6 (Damage Refactor) ◄── T0.1      T3.13 (Infinite Cannoballs) ◄── T1
                          T3.7 (Teleport) ◄── T1               T3.14 (Squirrel Console) ◄── T1
                                                               T3.15 (Save Editor) ◄── T1
                                                               T3.16 (Level Editor) ◄── T1
                                                               │
                          ┌────────────────────────────────────┼────────────────────────────┐
                          ▼                                    ▼                            ▼
PHASE 4: UI & UX          T4.1 (Config V2) ◄── T0.2,T0.3      T4.2 (Presets) ◄── T4.1
                          T4.3 (Favorites) ◄── T4.2           T4.4 (Hotkeys) ◄── T0.1
                          T4.5 (Menu expansion) ◄── T3.all,T4.2,T4.3,T4.4
                          T4.6 (Hooks update) ◄── T4.4,T4.5
                                                               │
                                                               ▼
PHASE 5: POLISH           T5.1 (Thread safety) ◄── T4.6
                          T5.2 (Doxygen comments) ◄── T3.all
                          T5.3 (Performance) ◄── T4.6
                          T5.4 (Debug/Release) ◄── T0.3
                          T5.5 (README + docs) ◄── T5.all
```

### Dependency Matrix

| Task | Blocks On | Blocks |
|------|-----------|--------|
| T0.1 utils refactor | — | T2.1, T2.2, T4.4, T5.1, T5.3 |
| T0.2 config.h expand | — | T4.1 |
| T0.3 build system | — | T4.1, T5.4 |
| T1 IDA analysis | T0.1 | T2.3, T3.all |
| T2.1 safe_patch | T0.1 | T3.6, T3.8-T3.13 |
| T2.2 AOB cache | T0.1 | T3.6 |
| T2.3 cheat_system | T1 | T3.2, T3.3, T3.10 |
| T3.1-T3.16 cheats | T1, T2.x | T4.5 |
| T4.1 Config V2 | T0.2, T0.3 | T4.2 |
| T4.2 Presets | T4.1 | T4.3, T4.5 |
| T4.3 Favorites | T4.2 | T4.5 |
| T4.4 Hotkeys | T0.1 | T4.5, T4.6 |
| T4.5 Menu expansion | T3.all, T4.2, T4.3, T4.4 | T4.6, T5.1 |
| T4.6 Hooks update | T4.4, T4.5 | T5.1, T5.3 |
| T5.1-T5.5 Polish | T4.6 | — |

---

## 2. Parallelization Groups

### Group A — Foundation (strictly sequential, 1 agent)
| Step | Task | Time |
|------|------|------|
| A1 | T0.1 — Utils refactoring | 30m |
| A2 | T0.2 — Config.h expansion | 15m |
| A3 | T0.3 — Build system | 15m |
| A4 | T1 — IDA analysis | 90m |

### Group B — Infrastructure (partially parallel, 2 agents)
| Step | Task | Time |
|------|------|------|
| B1 | T2.1 — safe_patch | 30m |
| B2 | T2.2 — AOB cache | 30m |
| B3 | T2.3 — Native cheat system activation | 45m |

**Parallel within B**: B1 and B2 can be done simultaneously (different files).

### Group C — Cheat Modules (MAX PARALLEL, N agents)
All T3.x tasks are independent. Each creates its own .c/.h pair.

| Task | Name | Files | Effort | Deps |
|------|------|-------|--------|------|
| C1 | T3.1 — NoClip | noclip.c/h | L | T1 |
| C2 | T3.2 — Stud Magnet | stud_magnet.c/h | M | T2.3 |
| C3 | T3.3 — Score Multiplier | score_mult.c/h | M | T2.3 |
| C4 | T3.4 — Memory Browser | memory_browser.c/h | XL | T0.1 |
| C5 | T3.5 — Free Camera | free_camera.c/h | XL | T1 |
| C6 | T3.6 — Damage System Refactor | refactor cheats.c/h | M | T2.1, T2.2 |
| C7 | T3.7 — Teleport | teleport.c/h | L | T1 |
| C8 | T3.8 — Infinite Ammo | infinite_ammo.c/h | M | T1 |
| C9 | T3.9 — Super Punch | super_punch.c/h | M | T1 |
| C10 | T3.10 — Always Gold | always_gold.c/h | S | T2.3 |
| C11 | T3.11 — Quick Combo | quick_combo.c/h | M | T1 |
| C12 | T3.12 — Mega Destruct | mega_destruct.c/h | L | T1 |
| C13 | T3.13 — Infinite Cannonballs | infinite_cannonballs.c/h | M | T1 |
| C14 | T3.14 — Squirrel Console | squirrel_console.c/h | XL | T1 |
| C15 | T3.15 — Save File Editor | save_editor.c/h | L | T1 |
| C16 | T3.16 — Level Editor Integration | level_editor.c/h | M | T1 |

**Total parallel agents possible: 16** (every cheat is independent)

### Group D — UI & UX (mostly sequential, 1-2 agents)
| Step | Task | Time | Deps |
|------|------|------|------|
| D1 | T4.1 — Config V2 | 45m | A2, A3 |
| D2 | T4.2 — Presets System | 60m | D1 |
| D3 | T4.3 — Favorites System | 30m | D2 |
| D4 | T4.4 — Hotkey Customization | 45m | A1 |
| D5 | T4.5 — Menu Expansion | 90m | C.all, D2, D3, D4 |
| D6 | T4.6 — Hooks Update | 30m | D4, D5 |

**Parallel within D**: D2+D3 vs D4 (different concerns). D5 gates on all D.

### Group E — Polish (parallel, 2 agents)
| Step | Task | Time | Deps |
|------|------|------|------|
| E1 | T5.1 — Thread safety | 30m | D6 |
| E2 | T5.2 — Doxygen comments | 60m | C.all |
| E3 | T5.3 — Performance | 30m | D6 |
| E4 | T5.4 — Debug/Release modes | 30m | A3 |
| E5 | T5.5 — README + docs | 30m | E.all |

**Parallel within E**: E1+E2+E3+E4 can be simultaneous.

---

## 3. Detailed Task List

---

### T0.1 — Utils Refactoring

**Files:** `src/utils.c`, `src/utils.h`  
**Effort:** M (80 lines new + 30 modified)  
**Deps:** None  
**Priority:** P0 (FOUNDATION)  
**Agent:** coder  
**Verification:** Compiles; all existing patches still work

**Changes:**
1. Add `safe_patch()` function:
   ```c
   typedef struct { DWORD addr; unsigned char *orig; size_t size; int active; } PatchState;
   int safe_patch_apply(PatchState *ps, DWORD addr, const void *patch, size_t size);
   int safe_patch_restore(PatchState *ps);
   ```
   - Encapsulates VirtualProtect + memcpy + original byte saving
   - Returns 0 on failure, logs error internally
   - Reduces ~30 lines per patch to ~3 lines

2. Add `find_pattern_cached()`:
   ```c
   typedef struct { DWORD result; const unsigned char *pattern; const char *mask; size_t len; } AobCache;
   DWORD find_pattern_cached(AobCache *cache, const unsigned char *pattern, const char *mask, size_t len);
   ```
   - Caches AOB results so re-scanning is O(1)
   - Each cheat module gets a static AobCache entry

3. Add `patch_mem_safe()` wrapper with error logging

4. Add Doxygen-style comments to all exported functions

5. Add `BPE_UNUSED` macro for param suppression

---

### T0.2 — Config.h Expansion

**Files:** `src/config.h`  
**Effort:** M (60 new defines)  
**Deps:** None  
**Priority:** P0 (FOUNDATION)  
**Agent:** coder  
**Verification:** Syntax check; all referenced in later tasks

**New defines (pre-populated from IDA findings):**
```c
// --- Native Cheat System ---
#define CHEAT_STRING_TABLE     0xC8F480  // Pointer table to cheat names
// Cheat string addresses (pre-scanned)
#define CHEAT_STUD_MAGNET_STR  0xA6EE10
#define CHEAT_SCOREX10_STR     0xA6ED18
// ... etc

// --- NoClip ---
#define NO_COLLISION_STR       0xBE0A38
#define NO_COLLISION_FUNC      0x6AC280  // Function that references the string

// --- Ammo ---
#define SET_AMMO_STR           0xC00328
#define MAX_AMMO_STR           0xBEDC7C

// --- Combo ---
#define EASY_COMBOS_STR        0xBF1AE4
#define PUNCH_ALWAYS_STUN_STR  0xBE333C

// --- Level Editor ---
#define LEVEL_EDITOR_CLASS_STR 0xBC108C

// --- Space for new offsets found during T1 ---
// Reserved: add all new findings here
```

---

### T0.3 — Build System Update

**Files:** `Makefile`, `src/d3d9_proxy.def`  
**Effort:** S (15 lines)  
**Deps:** None  
**Priority:** P0 (FOUNDATION)  
**Agent:** coder  
**Verification:** `make debug` builds with debug flags

**Changes:**
1. Add `DEBUG` build mode:
   ```makefile
   ifdef DEBUG
   CXXFLAGS = -O0 -g -static-libgcc -static-libstdc++ -D_DEBUG -DIMGUI_IMPL_WIN32_DISABLE_GAMEPAD
   else
   CXXFLAGS = -O2 -s -static-libgcc -static-libstdc++ -DIMGUI_IMPL_WIN32_DISABLE_GAMEPAD
   endif
   ```
   - Debug: `make DEBUG=1`
   - Release: `make` (default)

2. Update SRCS list — add all new .c/.cpp files

3. Version bump: `MOD_VER` from `"v4.0"` to `"v5.0"`

---

### T1 — IDA Analysis Sprint

**Files:** None (discovery only)  
**Effort:** XL (90 min of IDA work)  
**Deps:** None  
**Priority:** P0 (GATES ALL CHEATS)  
**Agent:** explore (IDA specialist)  
**Verification:** All 14 cheat addresses documented in a findings table

**Analysis tasks:**

| # | Target | Method | Output |
|---|--------|--------|--------|
| 1.1 | **NoClip** — xref `no_character_collisions` (0xbe0a38) | Decompile `sub_6AC280`, find the collision flag set/reset | Address of collision flag + patch strategy |
| 1.2 | **Cheat activation system** — xref the pointer table at 0xc8f480 | Search for functions that iterate the cheat string table | Address of cheat activation function + signature |
| 1.3 | **Stud Magnet activation** — trace `cheat_stud_magnet` usage | Follow from cheat table to activation logic | Call convention + params |
| 1.4 | **Score Multiplier activation** — trace `CHEAT_SCOREX10` usage | Same as 1.3 | Call convention + params |
| 1.5 | **Infinite Ammo** — xref `SetAmmo` (0xc00328), `max_ammo` (0xbedc7c) | Find ammo decrement code near SetAmmo calls | Address of ammo decrement + patch strategy |
| 1.6 | **Super Punch** — xref `punch_always_stun` (0xbe333c) | Decompile the punch damage function | Damage formula address + patch |
| 1.7 | **Always Gold** — xref `CHEAT_ALWAYS_SCORE_MULTIPLY` (0xbff3e0) | Trace score calculation in the cheat system | Score mult flag address |
| 1.8 | **Quick Combo** — xref `easy_combos` (0xbf1ae4) | Find combo counter increment | Combo flags + increment address |
| 1.9 | **Mega Destruct** — xref `CHEAT_SELFDESTRUCT` (0xbf89ec) | Understand destructible object check | Destructibility check address + patch |
| 1.10 | **Infinite Cannonballs** — xref `cannonball` (0xbe24cc) | Find cannonball count decrement | Cannonball decrement address |
| 1.11 | **Free Camera** — search for camera matrix/frustum functions | Search for strings like "camera", "lookat", D3DTS_VIEW manipulation | Camera struct offsets + patch |
| 1.12 | **Teleport** — find player position floats | Search for X/Y/Z position references, follow D3DTS_VIEW + D3DTS_WORLD | Player position float addresses |
| 1.13 | **Squirrel Console** — search for `sq_` imports or string refs | Check imports table for squirrel.dll/api; scan for `sq_pushroottable`, `sq_call` | Squirrel API entry points |
| 1.14 | **Save File Editor** — locate save/load functions | Search for strings like "save", "load", "slot", ".sav" | Save file I/O functions |

**Deliverable:** A `/home/matthieu/black-pearl-engine/IDA_FINDINGS_v5.md` document with all addresses, decompiled pseudocode snippets, and recommended patch strategies.

---

### T2.1 — Error Handling: safe_patch

**Files:** `src/utils.c`, `src/utils.h`  
**Effort:** S (40 lines)  
**Deps:** T0.1  
**Priority:** P0  
**Agent:** coder  
**Verification:** All cheats use safe_patch APIs

**Implementation:**
```c
typedef struct {
    DWORD addr;
    unsigned char *orig_bytes;  // heap-allocated when save succeeds
    size_t size;
    int active;                 // 1 = patched
} PatchRecord;

// Returns 1 on success, 0 on failure
int patch_apply(PatchRecord *pr, const void *new_bytes);
int patch_restore(PatchRecord *pr);
void patch_free(PatchRecord *pr);
```

Then refactor ALL existing patches in `cheats.c` and `water.c` to use `PatchRecord`. This eliminates ~30 repetitive VirtualProtect blocks across the codebase.

---

### T2.2 — AOB Scanning Cache

**Files:** `src/utils.c`, `src/utils.h`  
**Effort:** S (30 lines)  
**Deps:** T0.1  
**Priority:** P1  
**Agent:** coder  
**Verification:** Each cheat's pattern scanned only once

**Implementation:**
```c
typedef struct {
    DWORD result;
    const unsigned char *pattern;
    const char *mask;
    size_t len;
    int scanned;
} AobCache;

DWORD find_pattern_cached(AobCache *cache);
```
Static `AobCache` per cheat → pattern scan runs at most once per session.

---

### T2.3 — Native Cheat System Activation

**Files:** `src/native_cheats.c`, `src/native_cheats.h`  
**Effort:** M (120 lines)  
**Deps:** T1 (cheat activation address)  
**Priority:** P0  
**Agent:** coder  
**Verification:** Can call `native_cheat_set("cheat_stud_magnet", 1)` and it works

**Implementation:**
```c
// Call the game's native cheat activation function
int native_cheat_set(const char *cheat_name, int enable);
int native_cheat_get(const char *cheat_name);

// Convenience wrappers
void native_stud_magnet(int enable);
void native_score_multiplier(int multiplier);  // 0=off, 2,4,6,8,10
void native_always_score_multiply(int enable);
```

This requires the cheat activation function address from T1.2. The function likely takes a cheat name string and a boolean.

---

### T3.1 — NoClip

**Files:** `src/noclip.c`, `src/noclip.h`  
**Effort:** L (250 lines)  
**Deps:** T1.1 (collision flag address)  
**Priority:** P0  
**Agent:** coder  
**Verification:** Player walks through walls when enabled

**Strategy (from T1.1):**
Option A: If `sub_6AC280` sets a byte flag → NOP the setter
Option B: If it's a float collision radius → set to 0
Option C: If there's a `no_character_collisions` flag variable → force it to 1

**Implementation pattern:**
```c
void noclip_apply(void);
void noclip_remove(void);
int g_noclip_enabled;

void noclip_toggle(void) {
    g_noclip_enabled ? noclip_apply() : noclip_remove();
}
```
Uses `safe_patch` from T2.1.

---

### T3.2 — Stud Magnet

**Files:** `src/stud_magnet.c`, `src/stud_magnet.h`  
**Effort:** M (60 lines)  
**Deps:** T2.3 (native cheat system)  
**Priority:** P0  
**Agent:** coder  
**Verification:** Studs fly toward the player

**Implementation:**
```c
void stud_magnet_apply(void) { native_cheat_set("cheat_stud_magnet", 1); }
void stud_magnet_remove(void) { native_cheat_set("cheat_stud_magnet", 0); }
```

---

### T3.3 — Score Multiplier

**Files:** `src/score_mult.c`, `src/score_mult.h`  
**Effort:** M (80 lines)  
**Deps:** T2.3 (native cheat system)  
**Priority:** P0  
**Agent:** coder  
**Verification:** Score is multiplied by selected value

**Implementation:**
```c
int g_score_mult_value;  // 0=off, 2,4,6,8,10

void score_mult_apply(void);
void score_mult_remove(void);
```
Maps to `native_cheat_set("cheat_scorexN", 1/0)`.

---

### T3.4 — Memory Browser

**Files:** `src/memory_browser.c`, `src/memory_browser.h`  
**Effort:** XL (450 lines)  
**Deps:** T0.1 (utils)  
**Priority:** P1  
**Agent:** coder  
**Verification:** Can browse, search, and edit memory via menu

**UI:**
- Address input field
- Hex dump view (16 bytes per row, ASCII sidebar)
- Jump to address button
- Edit mode (type new bytes)
- Search for pattern (AOB)
- Save memory region to file
- Color coding: readable vs code vs invalid

**Implementation:**
```c
typedef struct {
    DWORD base;
    DWORD cursor;       // current offset from base
    int bytes_per_row;
    unsigned char buffer[4096];
} MemBrowser;

void mem_browser_open(void);
void mem_browser_render(void);   // called from menu
void mem_browser_navigate(DWORD addr);
void mem_browser_edit(DWORD addr, unsigned char val);
```

Integrated as its own menu tab (T4.5).

---

### T3.5 — Free Camera

**Files:** `src/free_camera.c`, `src/free_camera.h`  
**Effort:** XL (400 lines)  
**Deps:** T1.11 (camera addresses)  
**Priority:** P1  
**Agent:** coder  
**Verification:** Camera detaches from character, can fly around

**Controls (when enabled):**
- WASD: Move camera
- Mouse: Look around
- Scroll: Zoom
- Shift: Speed up

**Implementation approaches (from T1.11):**
Option A: Hook D3DTS_VIEW projection and inject custom view matrix
Option B: Overwrite camera position floats directly (if found)
Option C: NOP camera reset/update function

---

### T3.6 — Damage System Refactor

**Files:** `src/cheats.c`, `src/cheats.h`  
**Effort:** M (150 lines modified)  
**Deps:** T2.1, T2.2  
**Priority:** P1  
**Agent:** coder  
**Verification:** All 6 damage mods still work after refactor

**Changes:**
1. Convert all damage patch static vars to use `PatchRecord` from T2.1
2. Add mutual exclusion logic for conflicting cheats:
   - Invincibility XOR Damage Response Only (same address)
   - One-Hit-Kill = One Heart + Death NOP (combo)
3. Add AOB caching for damage/death addresses
4. Add `update_cheats()` optimization: skip if no state changed

---

### T3.7 — Teleport

**Files:** `src/teleport.c`, `src/teleport.h`  
**Effort:** L (250 lines)  
**Deps:** T1.12 (player position addresses)  
**Priority:** P2  
**Agent:** coder  
**Verification:** Can save position, teleport back

**Features:**
- Save position (3 float slots: X, Y, Z)
- Load position (write floats back)
- List of 10 save slots
- Labels per slot
- Persistent storage in bpe.cfg (via Config V2)

---

### T3.8 — Infinite Ammo

**Files:** `src/infinite_ammo.c`, `src/infinite_ammo.h`  
**Effort:** M (100 lines)  
**Deps:** T1.5 (ammo decrement address)  
**Priority:** P1  
**Agent:** coder  
**Verification:** Ammo count never decreases

**Strategy (from T1.5):**
- NOP the ammo decrement instruction, OR
- Force `max_ammo` variable to a high value, OR
- Hook `SetAmmo` to keep value at max

---

### T3.9 — Super Punch

**Files:** `src/super_punch.c`, `src/super_punch.h`  
**Effort:** M (120 lines)  
**Deps:** T1.6 (punch damage address)  
**Priority:** P1  
**Agent:** coder  
**Verification:** One punch defeats enemies

**Strategy (from T1.6):**
- Force damage value to max (instant kill), OR
- Set `punch_always_stun` flag, OR
- NOP enemy health decrement check

---

### T3.10 — Always Gold

**Files:** `src/always_gold.c`, `src/always_gold.h`  
**Effort:** S (40 lines)  
**Deps:** T2.3 (native cheat system)  
**Priority:** P1  
**Agent:** coder  
**Verification:** Always get gold stud rating

**Implementation:**
```c
void always_gold_apply(void) { native_cheat_set("cheat_always_score_multiply", 1); }
void always_gold_remove(void) { native_cheat_set("cheat_always_score_multiply", 0); }
```

---

### T3.11 — Quick Combo

**Files:** `src/quick_combo.c`, `src/quick_combo.h`  
**Effort:** M (100 lines)  
**Deps:** T1.8 (combo address)  
**Priority:** P1  
**Agent:** coder  
**Verification:** Combo counter maxes out instantly

**Strategy (from T1.8):**
- Set `easy_combos` flag, OR
- Force combo count to max value each frame, OR
- NOP combo increment cap check

---

### T3.12 — Mega Destruct

**Files:** `src/mega_destruct.c`, `src/mega_destruct.h`  
**Effort:** L (200 lines)  
**Deps:** T1.9 (mega destruct address)  
**Priority:** P1  
**Agent:** coder  
**Verification:** All objects/enemies are destructible

**Strategy (from T1.9):**
- NOP destructibility check (the "is this breakable?" function), OR
- Force all object flags to destructible, OR
- Activate `CHEAT_SELFDESTRUCT` native cheat

---

### T3.13 — Infinite Cannonballs

**Files:** `src/infinite_cannonballs.c`, `src/infinite_cannonballs.h`  
**Effort:** M (80 lines)  
**Deps:** T1.10 (cannonball address)  
**Priority:** P1  
**Agent:** coder  
**Verification:** Cannoball count never decreases

**Strategy (from T1.10):**
- NOP cannonball decrement instruction, OR
- Force count to max each frame
- Model after `Infinite Ammo` (T3.8) if same pattern

---

### T3.14 — Squirrel Console

**Files:** `src/squirrel_console.c`, `src/squirrel_console.h`  
**Effort:** XL (500 lines)  
**Deps:** T1.13 (Squirrel entry points)  
**Priority:** P2  
**Agent:** coder  
**Verification:** Can execute `print("hello")` from in-game console

**If Squirrel API is available in the binary:**
- Import sq_pushroottable, sq_pushstring, sq_call, sq_gettop
- Create an input box for script commands
- Execute arbitrary Squirrel code
- Display output in a scrollback buffer

**If Squirrel is not imported (2 options):**
1. Write a generic string injection console
2. Look for Lua/other scripting instead

**Fallback:** Text-based console that types into the game's built-in cheat console.

---

### T3.15 — Save File Editor

**Files:** `src/save_editor.c`, `src/save_editor.h`  
**Effort:** L (350 lines)  
**Deps:** T1.14 (save file functions)  
**Priority:** P2  
**Agent:** coder  
**Verification:** Can parse and edit save files from the menu

**Features:**
- Browse save file directory
- Parse save file header/metadata
- Edit stud count, brick count, completion %, etc.
- Save modified file (or backup first)
- Load save into memory via game's own load function

---

### T3.16 — Level Editor Integration

**Files:** `src/level_editor.c`, `src/level_editor.h`  
**Effort:** M (150 lines)  
**Deps:** T1 (existing LEVEL_EDITOR_CTOR_OFFSET = 0x18EB50)  
**Priority:** P2  
**Agent:** coder  
**Verification:** Can spawn LevelEditor from menu

**Implementation:**
```c
void level_editor_open(void);
void level_editor_close(void);
int level_editor_is_open(void);

typedef void (*LevelEditorCtor_t)(void*);
void level_editor_launch(void);
```
Use `LEVEL_EDITOR_CTOR_OFFSET` from config.h. Call the constructor + init. Provide menu item in the new Tools tab.

---

### T4.1 — Config V2

**Files:** `src/config_loader.c`, `src/config_loader.h`  
**Effort:** M (150 lines modified)  
**Deps:** T0.2, T0.3  
**Priority:** P2  
**Agent:** coder  
**Verification:** bpe.cfg has sections; old configs are migrated

**New format:**
```ini
# Black Pearl Engine v5 config
[Cheats]
infinite_studs=1
invincible=0

[Damage]
reverse_damage=0
one_hit_kill=0

[Presets]
current=my_favorite_setup

[Hotkeys]
noclip=VK_F3
stud_magnet=VK_F4

[Teleport]
slot_0_x=1234.5
slot_0_y=567.8
slot_0_z=90.1
slot_0_label=Checkpoint 1

[UI]
menu_x=0
menu_y=0
```

**Migration:** If no `[Cheats]` header → treat as old format and auto-upgrade.

---

### T4.2 — Presets System

**Files:** `src/presets.c`, `src/presets.h`  
**Effort:** M (200 lines)  
**Deps:** T4.1  
**Priority:** P1  
**Agent:** coder  
**Verification:** Can save/load/delete named presets

**Implementation:**
```c
#define MAX_PRESETS 20
#define PRESET_NAME_LEN 32

typedef struct {
    char name[PRESET_NAME_LEN];
    CheatsState state;  // snapshot of all cheat toggles
} Preset;

int preset_save(const char *name);
int preset_load(int index);
int preset_delete(int index);
int preset_get_count(void);
const Preset* preset_get_list(void);
```

Persistent storage: `bpe_presets.dat` or inline in bpe.cfg via Config V2.

---

### T4.3 — Favorites System

**Files:** `src/favorites.c`, `src/favorites.h`  
**Effort:** M (120 lines)  
**Deps:** T4.2  
**Priority:** P1  
**Agent:** coder  
**Verification:** Can mark cheats as favorites; shown first in menu

**Implementation:**
```c
#define MAX_FAVORITES 32

int favorite_toggle(int cheat_id);
int favorite_is_favorited(int cheat_id);
int favorite_get_count(void);
int favorite_get_ids(int *out, int max);
```

Stored in Config V2 as `[Favorites]` section.

---

### T4.4 — Hotkey Customization

**Files:** `src/hotkeys.c`, `src/hotkeys.h`  
**Effort:** L (300 lines)  
**Deps:** T0.1 (utils)  
**Priority:** P1  
**Agent:** coder  
**Verification:** Can assign F3-F12, Ctrl+key, Alt+key to any cheat

**Implementation:**
```c
#define MAX_HOTKEYS 32

typedef struct {
    int cheat_id;       // index into cheat list
    int vk_code;        // virtual key code
    int mod_ctrl;       // require Ctrl held
    int mod_alt;        // require Alt held
    int mod_shift;      // require Shift held
    char description[32];
} HotkeyBinding;

int hotkey_bind(int cheat_id, int vk, int ctrl, int alt, int shift);
int hotkey_unbind(int cheat_id);
int hotkey_get_for_cheat(int cheat_id, HotkeyBinding *out);
void hotkey_process_input(void);  // called from menu_update_input
```

**Menu Integration:**
- In the Cheat tab: "Bind Key" action when a cheat is selected
- Opens key capture mode (press any key)
- Shows assigned key next to cheat name

---

### T4.5 — Menu Expansion

**Files:** `src/menu.c`, `src/menu.h`  
**Effort:** XL (400 lines modified + 200 new)  
**Deps:** T3.all, T4.2, T4.3, T4.4  
**Priority:** P1  
**Agent:** coder  
**Verification:** All new cheats accessible; favorites/presets/hotkeys UI works

**New Tab Layout:**

| Tab | Items | Type |
|-----|-------|------|
| **Health** | Invincibility, Breathe Underwater | existing |
| **Damage** | Reverse Damage, One Heart, One-Hit-Kill, Damage Response, No Knockback, No Hit Reactions | existing |
| **Studs** | Infinite Studs, Custom Value, Custom Studs, Golden Bricks, **Stud Magnet**, **Score Multiplier**, **Always Gold** | 3 new |
| **Fun** | Super Speed, Y Velocity, Char Scale, **NoClip**, Time Freeze, Speed Mult, **Quick Combo**, **Super Punch** | 3 new |
| **Visual** | FPS, Debug, Remove Water | existing |
| **UltraWide** | Enable, Ratios | existing |
| **Ammo** | **Infinite Ammo**, **Infinite Cannonballs**, **Mega Destruct** | NEW tab |
| **Camera** | **Free Camera**, **Teleport** (save/load slots) | NEW tab |
| **Memory** | **Memory Browser** | NEW tab |
| **Tools** | **Squirrel Console**, **Save Editor**, **Level Editor** | NEW tab |
| **Presets** | Save, Load, Delete presets + **Favorites list** | NEW tab |
| **Hotkeys** | Assign keys to cheats | NEW tab |

**Tab count:** 6 → 12 tabs  
**Menu struct** needs `TAB_COUNT` updated, new item arrays added.

**Changes to `Item` struct:**
```c
typedef struct {
    const char *name;
    int type;           // 0=toggle, 1=value, 2=action, 3=separator, 4=hotkey
    void *val;
    int min, max;
    void (*action)(void);
    int favorite;       // NEW: marked as favorite
    int hotkey_vk;      // NEW: assigned hotkey VK code
} Item;
```

Menu rendering: Add "★" prefix for favorites, key binding display.

---

### T4.6 — Hooks Update

**Files:** `src/hooks.c`, `src/hooks.h`  
**Effort:** M (80 lines)  
**Deps:** T4.4, T4.5  
**Priority:** P1  
**Agent:** coder  
**Verification:** Hotkeys work; Free camera controls don't conflict with game

**Changes:**
1. Add hotkey processing in `menu_update_input()`:
   ```c
   void menu_update_input(void) {
       // ... existing code ...
       hotkey_process_input();  // new: check all bound hotkeys
   }
   ```
2. Free camera: block game input when free cam is active
3. NoClip: hook movement update to prevent falling through floor
4. Add new include for hotkeys.h

---

### T5.1 — Thread Safety

**Files:** `src/cheats.c`, `src/menu.c`, `src/config_loader.c`  
**Effort:** S (30 lines)  
**Deps:** T4.6  
**Priority:** P2  
**Agent:** coder  
**Verification:** No race conditions observed; no crashes

**Changes:**
1. Add `CRITICAL_SECTION g_cheats_lock` for `g_cheats` access
2. Lock around `update_cheats()` calls
3. Lock around config save/load
4. Windows critical sections (no atomic perf impact on x86)

---

### T5.2 — Doxygen Comments

**Files:** All `.h` and `.c` files  
**Effort:** M (150 line additions)  
**Deps:** T3.all  
**Priority:** P2  
**Agent:** coder  
**Verification:** Every exported function has a doc comment

**Style:**
```c
/**
 * @brief Apply/Remove infinite ammo patch
 * @param enable 1=apply, 0=remove
 * @return 0 on success, -1 on failure
 *
 * NOPs the ammo decrement instruction at AMMO_DEC_ADDR.
 * Uses AOB caching for version resilience.
 */
```

---

### T5.3 — Performance Optimization

**Files:** `src/cheats.c`, `src/utils.c`  
**Effort:** S (30 lines)  
**Deps:** T4.6  
**Priority:** P2  
**Agent:** coder  
**Verification:** Same FPS with/without cheats enabled

**Changes:**
1. `update_cheats()`: compare state before applying:
   ```c
   if (g_cheats.noclip != g_prev_cheats.noclip) {
       if (g_cheats.noclip) noclip_apply(); else noclip_remove();
   }
   ```
2. Memcmp the whole `CheatsState` struct, only update on change
3. Lazy AOB scanning: scan on first enable, cache result

---

### T5.4 — Debug/Release Build Modes

**Files:** `Makefile`  
**Effort:** S (already in T0.3)  
**Deps:** T0.3  
**Priority:** P2  
**Agent:** coder  
**Verification:** `make` = release (small .dll); `make DEBUG=1` = debug (with symbols)

---

### T5.5 — README + Documentation Update

**Files:** `README.md`, `PLAN_v5.md` (this file)  
**Effort:** M (100 lines)  
**Deps:** T5.all  
**Priority:** P2  
**Agent:** coder  
**Verification:** All new features documented

---

## 4. Implementation Order (Sequential Execution Plan)

### Sprint 1: Foundation (1 agent, ~2.5h)
```
T0.1 → T0.2 → T0.3 → T1
```
**Checkpoint M1:** Project compiles, IDA findings written to file.

### Sprint 2: Infrastructure (2 agents parallel, ~1h)
```
Agent A: T2.1 + T2.2
Agent B: T2.3  (after T1 completes)
```
**Checkpoint M2:** safe_patch, AOB cache, native cheat activation all working.

### Sprint 3: Cheat Modules (N agents, ~3h total)
```
All T3.x tasks in parallel, each by a separate agent.
P0 cheats first (T3.1-T3.3), then P1 (T3.4-T3.13), then P2 (T3.14-T3.16).
```
**Checkpoint M3:** All 16 cheats compile individually.

### Sprint 4: UI Integration (1-2 agents, ~4h)
```
T4.1 → T4.2 → T4.3 + T4.4 (parallel) → T4.5 → T4.6
```
**Checkpoint M4:** Menu shows all 12 tabs, hotkeys work, presets save/load.

### Sprint 5: Polish (2 agents, ~1h)
```
T5.1 + T5.2 + T5.3 + T5.4 (parallel) → T5.5
```
**Checkpoint M5 (FINAL):** Release build `d3d9.dll` ready.

---

## 5. Risk Assessment

| # | Risk | Likelihood | Impact | Mitigation |
|---|------|-----------|--------|------------|
| R1 | **IDA analysis inconclusive** — cheat activation function not found | Medium | High | Fallback to binary patching (AOB scan for known patterns). Use existing stud/health patch patterns as template. |
| R2 | **Native cheat activation corrupts game state** | Low | High | Always save original bytes. Test on non-saved game first. Add panic button (F10 = disable all). |
| R3 | **Memory browser crashes when reading invalid addresses** | Medium | Medium | Use `IsBadReadPtr` / `VirtualQuery` before every read. Address validation (no kernel space reads). |
| R4 | **Free camera causes rendering glitches** | High | Medium | Hook Reset to restore camera. Make camera changes toggleable per-frame. |
| R5 | **Hotkey conflicts with game controls** | Medium | Low | Default to F-keys only (not used by game). Let user customize in hotkey UI. |
| R6 | **Menu too large for 720p screens** | Low | Medium | Make `MENU_H` dynamic based on resolution. Add scrollbar. |
| R7 | **Squirrel Console no Squirrel API in binary** | High | High | Check for Squirrel DLL imports in IDA session. If absent, fallback to simple command injection or drop feature. |
| R8 | **Damage system refactor breaks existing cheats** | Medium | Critical | Write a test harness: enable all damage mods, verify patch state array. Test ONE at a time. |
| R9 | **Presets save incompatible state** | Low | Medium | Validate CheatsState struct size/version. Add version field. |
| R10 | **Ammo/Combo addresses version-specific** | Medium | Medium | Always implement AOB scan fallback before hardcoded addresses. |

---

## 6. File Mapping (Complete)

### New Files to Create
```
src/noclip.c / .h              — NoClip cheat
src/stud_magnet.c / .h         — Stud Magnet (native cheat)
src/score_mult.c / .h          — Score Multiplier (native cheat)
src/memory_browser.c / .h      — In-game hex editor
src/free_camera.c / .h         — Free Camera
src/infinite_ammo.c / .h       — Infinite Ammo
src/super_punch.c / .h         — Super Punch
src/always_gold.c / .h         — Always Gold (native cheat)
src/quick_combo.c / .h         — Quick Combo
src/mega_destruct.c / .h       — Mega Destruct
src/infinite_cannonballs.c / .h — Infinite Cannonballs
src/teleport.c / .h             — Teleport
src/squirrel_console.c / .h    — Squirrel Console
src/save_editor.c / .h         — Save File Editor
src/level_editor.c / .h        — Level Editor Integration
src/native_cheats.c / .h       — Native cheat activation (shared by 3 modules)
src/presets.c / .h              — Presets system
src/favorites.c / .h            — Favorites system
src/hotkeys.c / .h              — Hotkey customization
```

### Existing Files to Modify
```
src/cheats.c / .h               — Damage refactor, new CheatsState fields, new include
src/menu.c / .h                 — 12 tabs, favorites display, hotkey display
src/hooks.c / .h                — Hotkey processing, free camera input
src/config_loader.c / .h        — Config V2 sections, preset storage
src/config.h                    — All new addresses, offsets, patterns
src/utils.c / .h                — safe_patch, AOB cache, doxygen
src/d3d9_proxy.c                — Update version string, new init calls
Makefile                        — All new .c/.cpp files, debug/release modes
```

---

## 7. Build Milestones

| Milestone | Deliverable | Verification |
|-----------|------------|--------------|
| **M0** | This plan accepted | Plan reviewed by @boss |
| **M1** | Foundation done | `make` succeeds with new utils + config.h |
| **M2a** | safe_patch deployed | All 20 existing cheats using PatchRecord; no regressions |
| **M2b** | Native cheat system works | `native_cheat_set("cheat_stud_magnet",1)` works in-game |
| **M3** | All cheats compile | `make` succeeds; `nm d3d9.dll` shows all new symbols |
| **M4** | Full menu works | 12 tabs navigable; presets save/load; hotkeys trigger cheats |
| **M5** | Release build | `make` produces optimized `d3d9.dll`; README updated |

---

## 8. Agent Assignment Summary

| Agent Type | Tasks | Count |
|-----------|-------|-------|
| **explore** (IDA) | T1 | 1 task |
| **coder** (cheat dev) | T0.1-T0.3, T2.1-T2.3 | 5 tasks |
| **coder** (cheat dev) | T3.1-T3.16 | 16 parallel tasks |
| **coder** (UI dev) | T4.1-T4.6 | 6 tasks |
| **coder** (polish) | T5.1-T5.5 | 5 tasks |
| **reviewer** | Review M2-M5 checkpoints | 1 per milestone |

**Peak parallelism:** 17 agents (16 cheat modules + 1 UI)

---

## 9. Self-Upgrade Log

### What I learned from this planning session:
1. **IDA xref discovery pattern**: Searching for game's native cheat strings (`CHEAT_*`) reveals existing cheat infrastructure. Many "new features" are actually just activating existing game code — much safer than binary patching.

2. **Massive parallelization opportunity**: When creating 16 new modules that are file-independent (each has its own .c/.h), they can ALL run in parallel. This is the "batch of leaves" pattern — once the trunk is done, every leaf node is independent.

3. **Plan-during-analysis**: Doing a binary survey (`ida_survey_binary`) and targeted string/regex searches DURING planning (not after) reveals critical shortcuts. Found 91 native cheats that reshape the entire implementation strategy.

4. **Dependency-aware sprint planning**: Cheats that depend on T1 (IDA analysis) cannot start until T1 is done — but T1 can be done by an explore agent while a coder does T0.x, then T2.x, maximizing pipeline utilization.

### Log entry for EVOLVING.md:
```
2026-06-28 @planner IDA preflight pattern — Always run ida_survey_binary + targeted string search for native cheat/system features BEFORE writing the plan. Found 91 built-in cheat strings that reshape P0/P1 strategy entirely. — Impact: HIGH
→ useful for: @planner, @spec-writer
```
