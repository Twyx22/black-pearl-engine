# AGENTS.md — Black Pearl Engine

d3d9.dll proxy mod menu (32-bit x86) for LEGO Pirates of the Caribbean. C + vendored MinHook/ImGui. No tests, no lint, no CI.

## Build

- Toolchain: `i686-w64-mingw32-g++` only. Linux: `sudo apt install g++-mingw-w64-i686`. Currently **not installed** in this env — install before building.
- `make clean && make` — release (`-O2 -s`); `make DEBUG=1` — debug (`-O0 -g`, debug console). Output: `d3d9.dll` at repo root (gitignored).
- `make install` / `make run` are **Windows-only** (`copy /y`, hardcoded `C:\GOG Games\...` path) — broken on Linux, never use here.
- Verify by compile only: `make clean && make` must succeed. Cannot run without the game on Windows/Wine (`WINEDLLOVERRIDES="d3d9=n,b"`, DLL next to `LEGOPirates.exe`).
- New cheat module not linking? Check `Makefile` `SRCS` — every `src/*.c` must be listed; missing entries = undefined refs. Missing Win32 API at link = add lib to `LDFLAGS` (e.g. `CoTaskMemFree` needs `-lole32`).

## Architecture

- Entry: `src/d3d9_proxy.c` (`DllMain`) → `load_config`, `install_time_hooks`, `native_cheats_init`. Runtime: cheats update in `EndScene`, ImGui renders in `Present` (`src/hooks.c`, `src/menu.c`, 12 tabs, `TAB_COUNT` in `menu.h`).
- One cheat = one `src/<name>.c/.h` pair with `apply`/`remove` functions, wired through `CheatsState` (`src/cheats.h`), `update_cheats()` dirty-flag dispatch (`src/cheats.c`), and a tab `Item` (`src/menu.c`). Adding a cheat touches all four + `Makefile` `SRCS` + `src/config.h` offsets.
- `src/config.h` holds all game addresses as **module-base-relative offsets** (preferred base `0x400000`). `src/utils.h` is the shared core: `PatchRecord`, `AobCache`, `safe_read/safe_write`.

## Patch conventions (follow these)

- Binary patches: always `patch_apply`/`patch_restore` with `PatchRecord` — never open-code `VirtualProtect`.
- AOB scans: always `find_pattern_cached` + `static AobCache` (scan once per session). Prefer `.text` AOB patterns over hardcoded offsets; hardcoded addresses are fallbacks only.
- Prefer native cheats (`native_cheat_set("cheat_...", 1)` via the strcmpi hook in `src/native_cheats.c`) over binary patching when a game string exists. Uncertain cheat? 3-tier fallback: native → AOB NOP → per-frame force-write, each tier logging independently.
- Constraint: **Invincibility XOR Damage Response Only** — same patch site (`DEC [ebp+0xE26]`), enforce mutual exclusion like existing damage mods.

## Hard rules (fix/bug-sweep)

- New cheat MUST be dispatched in `update_cheats()` (`src/cheats.c`) — verify with `grep` that its apply/remove is called, else dead toggle.
- UI rebuild: never `strdup` without paired `free`; never allocator rebuild per frame (leak in `menu.c`).
- No `__try`/`__except` (MinGW-GCC unsupported) — `VirtualQuery` + `safe_read` only.
- `.c` files compile as C++ (`CC=CXX`) — `REFGUID` is a reference, not a pointer.

## Gotchas

- Vendored `lib/imgui` is v1.91+: `TextColored` takes `ImVec4` (wrap `IM_COL32` with `ImGui::ColorConvertU32ToFloat4`), `Checkbox` takes `bool*` (not `int*`).
- `lib/minhook`, `lib/imgui`: vendored, do not upgrade or reformat.
- Config V2 (`src/config_loader.c`): `bpe.cfg` section INI with `# bpe_cfg_v2` marker; no `[Cheats]` header = v1 file, auto-migrate on next save (menu close via F1). Teleport lives in separate `bpe_teleport.cfg`; presets are `bpe_preset_<name>.cfg` (max 20); favorites/hotkeys max 32 each.
- Docs: `README.md` is the feature/address reference; `EVOLVING.md` holds hard-earned lessons (check before new cheat work); `PLAN_v5.md` is historical planning, not current truth.
