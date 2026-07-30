# Moraff's World — 1024×768 Native Edition

An unofficial native Windows preservation port and Enhanced Edition of
**Moraff's World**, the DOS dungeon crawler created by **Steve Moraff** and
published by **MoraffWare**.

The original game, its name, design, text, graphics, fonts, maps, and data
remain the work and property of Steve Moraff / MoraffWare. This fan project is
not affiliated with or endorsed by MoraffWare. It does not include the
original game files.

## Quick installation

1. Extract this release into a new writable directory.
2. From your legally obtained original Moraff's World installation, copy the
   following ten files into that same directory:

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

3. The files must sit directly beside `moraffs_world.exe`, not in a
   subdirectory.
4. Run `moraffs_world.exe`.

The port verifies the size, CRC-32, and SHA-256 checksum of `MW.EXE` and
`WORLD.EXE` before it starts. See
[ORIGINAL_FILES_REQUIRED.md](ORIGINAL_FILES_REQUIRED.md) for the approved
executable variants and exact tested checksums of all ten files.

## System requirements

- 64-bit Windows 10 or Windows 11
- A 1024×768 or larger display
- Keyboard; mouse supported
- A writable game directory for character saves, dungeon state, and bestiary
  progress
- The ten original files listed above

SDL is statically linked, so no separate SDL DLL is required.

## Classic and Enhanced experiences

New characters can select:

- **Classic** — the original 251-floor scale and original progression limits.
- **Enhanced** — an extended 1,000-floor dungeon with additional enemies,
  bosses, equipment, spells, magic items, relics, and late-game quests.

The Enhanced additions do not replace Steve Moraff's original content. They
extend the native port's optional Enhanced mode.

## Useful documentation

- `EXPERIENCE_MODES.md` — Classic versus Enhanced behavior
- `DEEP_DUNGEON.md` — Enhanced floors, bosses, equipment, and relics
- `DEEP_SPELLS.md` — Enhanced spell catalog
- `HOTKEYS.md` — controls and optional diagnostic shortcuts
- `CREDITS.md` — original-game, port, and preservation credits
- `THIRD_PARTY_NOTICES.md` — SDL and build-tool acknowledgements
- `LICENSE_PORT.txt` — license for the port code only

## Saves and local files

The port uses only files beside its executable. Character saves, generated
dungeons, monster state, bestiary progress, and other runtime data are created
in the game directory. Do not install it under a location where your account
cannot write files.

Back up the entire directory to preserve all characters and generated dungeon
state.

## Troubleshooting

### “Moraff's World files missing”

One or more of the ten required original files is absent or stored in a
subdirectory. Copy all ten directly beside `moraffs_world.exe`.

### “Original game verification failed”

`MW.EXE` or `WORLD.EXE` is not one of the approved original variants. Compare
its size, CRC-32, and SHA-256 values with `ORIGINAL_FILES_REQUIRED.md`. A
renamed, patched, truncated, or unknown executable will not pass.

### The game cannot save

Move the complete game directory somewhere writable, such as a folder under
Documents, then run it again.

## Legal notice

This package contains only the native port executable and its project
documentation. Original MoraffWare files must be supplied by the user and are
not licensed under the port's MIT license.
