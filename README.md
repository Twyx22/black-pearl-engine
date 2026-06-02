# Black Pearl Engine

> **WIP** — Mod menu / cheat engine for **LEGO Pirates of the Caribbean: The Video Game**

![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20(Wine%2FProton)-blue)
![Arch](https://img.shields.io/badge/Architecture-x86%20(32--bit)-orange)
![Status](https://img.shields.io/badge/Status-WIP-yellow)

## What Works

| Cheat | Status | Notes |
|-------|--------|-------|
| **Infinite Studs** | ✅ | NOPs the `sub ebx,eax` + `sbb esi,edx` at fixed instruction address |
| **Custom Stud Value** | ✅ | Writes to display address every frame |
| **Golden Bricks** | ✅ | Forces golden brick count |
| **Menu UI** | ✅ | Golden theme, 4 tabs, keyboard navigation, text input for values |
| **FPS Counter** | ✅ | Real-time FPS display |
| **Debug Info** | ✅ | Entity string table viewer |
| **Config File** | ✅ | Saves/loads cheat toggles from `bpe.cfg` |

## What's Broken / WIP

| Cheat | Status | Issue |
|-------|--------|-------|
| **Super Speed** | ❌ | Addresses `0x00E24C50/0x00E24C54` may be wrong in this version |
| **Super Jump** | ❌ | Addresses `0x00E24C4C` may be wrong |
| **Moon Jump** | ❌ | Address `0x00E24C48` may be wrong |
| **Invincibility** | ❌ | Health decrement scanner doesn't find the right offsets |
| **Input Blocking** | ⚠️ | WndProc + PeekMessageA hooks; unreliable under Wine/Proton |
| **Time Freeze** | ⚠️ | IAT hooks on GetTickCount/QueryPerformanceCounter; untested |
| **NoClip** | ❌ | Placeholder |
| **Stud Magnet** | ❌ | Placeholder |

Most addresses were found via Cheat Engine and are specific to the GOG/retail build. They will likely differ for Steam or other versions.

## Quick Start

1. Copy `d3d9.dll` next to `LEGOPirates.exe`
2. For Wine/Proton: `export WINEDLLOVERRIDES="d3d9=n,b"`
3. Launch the game
4. Press **F1** to open the menu

### Build from Source

```bash
sudo apt install g++-mingw-w64-i686
make clean && make
make install   # copies to game directory
```

## Controls

| Key | Action |
|-----|--------|
| **F1** | Toggle Menu (saves config on close) |
| **F2** | Toggle Debug Info |
| **↑ / ↓** | Navigate items |
| **← / →** | Switch tabs |
| **Enter** | Toggle cheat / Edit value |
| **Escape** | Cancel editing |
| **Backspace** | Delete char in input |
| **0-9 / Numpad** | Type numbers |

## Config

Settings auto-save on menu close (F1) and on game exit.  
Edit `bpe.cfg` to set default values:

```
infinite_studs=1
force_golden_bricks=1
golden_brick_value=85
```

## Known Addresses

All offsets are `_LEGOPirates.exe + offset` from module base.

| Purpose | Instruction / Data | Notes |
|---------|-------------------|-------|
| Stud subtract | `0x006B1BC5` (sub ebx,eax) + `0x006B1BC7` (sbb esi,edx) | NOP'd for infinite studs |
| Stud display | `0x0369B660` (4 bytes) | Forced each frame for custom value |
| Golden bricks | `0x00F776E4` (4 bytes) | Forced each frame |
| Entity table | `0x00C8F400` | Pointer array to entity strings |

## Project Structure

```
src/
├── config.h             # Memory addresses
├── config_loader.c/h    # bpe.cfg save/load
├── cheats.c/h           # All cheat implementations
├── menu.c/h             # Overlay menu (D3D9)
├── hooks.c/h            # EndScene, Reset, WndProc, IAT hooks
├── input.c/h            # DirectInput hooks (WIP)
├── utils.c/h            # Logging, memory patching, IAT helpers
├── d3d9_proxy.c         # DllMain + Direct3DCreate9 proxy
└── d3d9_proxy.def       # DLL exports
```

## License

MIT
