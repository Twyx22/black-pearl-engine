# Black Pearl Engine v4.0

> Advanced mod menu & cheat engine for **LEGO Pirates of the Caribbean: The Video Game**

![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20(Wine%2FProton)-blue)
![Arch](https://img.shields.io/badge/Architecture-x86%20(32--bit)-orange)
![License](https://img.shields.io/badge/License-MIT-green)

---

## Overview

**Black Pearl Engine** is a D3D9 proxy DLL that injects a fully-featured mod menu into LEGO Pirates of the Caribbean via `EndScene` hooking. It works on both native Windows and Linux through Wine/Proton.

### Key Features

- Beautiful in-game overlay menu with golden theme
- Runtime memory patching engine
- Input blocking when menu is open
- Text input boxes for custom values
- Dynamic code scanner for auto-discovering cheat locations

---

## Screenshots

*Menu overlay with tabs: Health, Studs, Fun, Visual*

---

## Installation

### Prerequisites

- MinGW-w64 cross-compiler (`i686-w64-mingw32-g++`)
- `d3d9` and `d3dx9` development headers
- `make`

### Build

```bash
make clean && make
```

This produces `d3d9.dll` in the project root.

### Install

1. Copy `d3d9.dll` to your game directory (next to `LEGOPirates.exe`)
2. If using Wine/Proton, set the DLL override:
   ```bash
   export WINEDLLOVERRIDES="d3d9=n,b"
   ```
3. Launch the game
4. Press **F1** in-game to open the mod menu

---

## Controls

| Key | Action |
|-----|--------|
| **F1** | Toggle Menu |
| **F2** | Toggle Debug Info |
| **↑ / ↓** | Navigate items |
| **← / →** | Change tabs |
| **Enter** | Toggle cheat / Enter edit mode |
| **Escape** | Cancel text input |
| **Backspace** | Delete character in input |
| **0-9 / Numpad** | Type numbers in input box |

> **Note:** Game controls are automatically blocked when the menu is open!

---

## Features

### Health Tab
- **Invincibility** — Prevents health decrement (scans `.text` for health patterns)
- Extra Hearts *(placeholder)*
- Regenerate *(placeholder)*
- Breathe Underwater *(placeholder)*

### Studs Tab
- **Infinite Studs** — Patches all `SUB [0x00E41868]` instructions in real-time
- **Force Custom Value** — Locks display to a custom amount
- **Custom Studs** — Editable value (0 - 999,999,999) with text input box
- Stud Magnet *(placeholder)*
- **Score Multiplier** — x1 to x10

### Fun Tab
- Super Speed *(placeholder — requires dynamic offset discovery)*
- Super Jump *(placeholder)*
- Moon Jump *(placeholder)*
- NoClip *(placeholder)*
- Time Freeze *(placeholder)*

### Visual Tab
- **FPS Counter** — Shows real-time FPS
- **Debug Info** — Displays game strings and entities (F2)

---

## Technical Details

### Architecture

```
┌─────────────────┐
│  LEGOPirates.exe │
│   (game process) │
└────────┬────────┘
         │ loads
         ▼
┌─────────────────┐
│   d3d9.dll      │ ◄── Our proxy (this project)
│  (proxy/wrapper)│
└────────┬────────┘
         │ forwards to
         ▼
┌─────────────────┐
│  system d3d9.dll│
│   (real D3D9)   │
└─────────────────┘
```

### Hooking Method

1. **Proxy Loading**: Game loads our `d3d9.dll` instead of the system one
2. **CreateDevice Hook**: We hook `CreateDevice` to capture the `IDirect3DDevice9`
3. **Vtable Swap**: We copy the device's vtable (512 entries for DXVK compatibility) and replace `EndScene` (index 42)
4. **Input Hook**: IAT hook on `PeekMessageA` to block game inputs when menu is open

### Memory Patching Engine

- **Stud Patch**: Dynamically scans `.text` section for `SUB DWORD PTR [0x00E41868]` instructions and replaces them with NOPs
- **Health Patch**: Scans for `DEC`/`SUB` patterns on health structure offsets
- **Custom Forcer**: Directly writes to the stud display address (`0x0359B660`) every frame

### Known Addresses (LEGO Pirates PC)

| Purpose | Address | Type |
|---------|---------|------|
| Stud Counter (internal) | `0x00E41868` | DWORD |
| Stud Display | `0x0359B660` | DWORD |
| Entity Table | `0x00C8F400` | Pointer array |
| Stud SUB #1 | `0x00453664` | Code |
| Stud SUB #2 | `0x00453684` | Code |
| Stud SUB #3 | `0x004536C0` | Code |

---

## Development

### Project Structure

```
black-pearl-engine/
├── src/
│   ├── d3d9_proxy.c      # Main proxy code with menu & hooks
│   └── d3d9_proxy.def    # DLL exports definition
├── Makefile              # Build configuration
├── README.md             # This file
└── d3d9.dll              # Compiled output (after make)
```

### Adding New Cheats

1. Add toggle/int variable to `g_cheats` struct
2. Add menu item to the appropriate tab array
3. Implement patch/scan logic in `update_cheats()`
4. Rebuild with `make`

### Debug

Check `bpe.log` in the game directory for runtime logs:
- Hook status
- Patch applications
- Entity scan results
- Error messages

---

## Limitations & TODO

- [x] Menu UI with tabs and selection
- [x] Infinite Studs via runtime patching
- [x] Custom stud value with text input
- [x] Input blocking when menu is open
- [ ] Super Speed (needs physics offset discovery)
- [ ] Super Jump / Moon Jump (needs gravity offset)
- [ ] NoClip (needs collision disable)
- [ ] Time Freeze (needs game clock offset)
- [ ] Health patching (offset varies by build)
- [ ] Config save/load
- [ ] Stud magnet implementation

---

## Troubleshooting

**Menu doesn't appear**
- Check `bpe.log` for "Hooked!" message
- Ensure `d3d9.dll` is in the same folder as `LEGOPirates.exe`
- For Wine/Proton: verify `WINEDLLOVERRIDES="d3d9=n,b"` is set

**Game crashes on launch**
- Make sure you're using the 32-bit (x86) version of the DLL
- Check that you have the correct DirectX runtime installed

**Custom studs value doesn't stick**
- The display address may differ between game versions
- Use Cheat Engine to find your current stud value and update `STUD_DISPLAY_ADDR`

---

## Credits

- **TT Games** — For the amazing LEGO Pirates of the Caribbean
- **MinGW-w64** — Cross-compilation toolchain
- **Microsoft** — DirectX 9 SDK

## License

MIT License — See LICENSE file for details.

---

> *"Why is the rum gone?"* — Because we spent it all on studs.
