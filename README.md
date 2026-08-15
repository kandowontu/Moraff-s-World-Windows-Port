# Moraff's World — Native Windows Edition

Version 1.1 is an unofficial native Windows preservation port and optional
Enhanced Edition of **Moraff's World**, the DOS dungeon crawler created by
**Steve Moraff** and published by **MoraffWare**.

The original game, its name, design, text, graphics, fonts, maps, and data
remain the work and property of Steve Moraff / MoraffWare. This fan project is
not affiliated with or endorsed by MoraffWare, and does not distribute the
original game files.

## Download and installation

Download the Windows x64 ZIP from the
[latest GitHub release](https://github.com/kandowontu/Moraff-s-World-Windows-Port/releases/latest),
extract it into a writable directory, and copy these ten files from a legally
obtained Moraff's World installation directly beside `moraffs_world.exe`:

- `MW.EXE`
- `WORLD.EXE`
- `DUNG.BIN`
- `WORLDMAP.BIN`
- `H.BIN`
- `WORLD.PIC`
- `WALL.PIC`
- `360X480.FNT`
- `320X200.FNT`
- `ROLL.TXT`

The port verifies the size, CRC-32, and SHA-256 of `MW.EXE` and `WORLD.EXE`
before starting. See
[`ORIGINAL_FILES_REQUIRED.md`](port/ORIGINAL_FILES_REQUIRED.md) for the
approved executable variants and tested checksums.

## Version 1.1 highlights

- A substantially expanded fidelity pass based on `WORLD.ASM`/`WORLD.C`,
  including original-style input repeat timing, keypad behavior, combat flow,
  item dialogs, spell selectors, title presentation, death handling, dungeon
  traversal, wilderness movement, doors, ladders, traps, loot, and shops.
- Classic Experience preserves the original 251-floor scale and catalog.
- Enhanced Experience extends the dungeon to 1,000 floors and player
  progression to level 3,000, with new monsters, quest bosses, equipment,
  relics, races, classes, and 60 late-game spells plus matching magic items.
- A separate ten-save Colosseum roguelike mode with randomized opponents,
  champion rounds, reward drafts, healing, perks, and career statistics.
- A live-formula battle simulator, expanded trainer, detailed Beastiary,
  game-statistics screen, model/palette viewer, mouse support, and documented
  optional diagnostic shortcuts.
- Original-game assets are always loaded from relative paths beside the native
  executable; no installer, registry entry, or hard-coded developer path is
  used.

See the packaged guides for complete details:

- [`README_RELEASE.md`](port/README_RELEASE.md) — installation and operation
- [`RELEASE_NOTES.md`](port/RELEASE_NOTES.md) — Version 1.1 changes
- [`EXPERIENCE_MODES.md`](port/EXPERIENCE_MODES.md) — Classic versus Enhanced
- [`DEEP_DUNGEON.md`](port/DEEP_DUNGEON.md) — Enhanced progression and rewards
- [`DEEP_SPELLS.md`](port/DEEP_SPELLS.md) — Enhanced spell catalog
- [`COLOSSEUM.md`](port/COLOSSEUM.md) — isolated roguelike arena mode
- [`HOTKEYS.md`](port/HOTKEYS.md) — controls and optional diagnostic shortcuts
- [`CREDITS.md`](port/CREDITS.md) and
  [`THIRD_PARTY_NOTICES.md`](port/THIRD_PARTY_NOTICES.md) — attribution

## Building from source

Requirements: 64-bit Windows, CMake 3.16 or newer, Ninja, a C11 compiler, and
PowerShell. SDL2 2.30.12 is fetched and statically linked by CMake.

```powershell
cd port
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

For a distributable package that intentionally excludes copyrighted original
files:

```powershell
.\package_release.ps1 -Version 1.1.1
```

The port source is covered by [`LICENSE_PORT.txt`](port/LICENSE_PORT.txt).
Original MoraffWare code and assets are not covered by that license.

Development and reverse-engineering assistance used OpenAI Codex GPT-5-series
models. Full original-game, port, and open-source-library credits are in
[`CREDITS.md`](port/CREDITS.md).
