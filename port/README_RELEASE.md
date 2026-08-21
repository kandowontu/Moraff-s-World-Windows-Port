# Moraff's World — Native Windows Edition

Version 1.1.01 for 64-bit Windows.

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
- A 640×480 or larger display (1024×768 recommended)
- Keyboard; mouse supported
- A writable game directory for character saves, dungeon state, and bestiary
  progress
- The ten original files listed above

SDL is statically linked, so no separate SDL DLL is required.

## Original video modes

The selector exposes all twelve `WORLD.EXE` display-driver branches. Repeated
resolutions remain separate because their renderers are not interchangeable:
320×200 has CGA 4-color, EGA 16-color, and MCGA/VGA 256-color paths;
640×480 has planar 16-color and VESA 256-color paths; and 1024×768 has planar
16-color plus two original 256-color driver choices. The other drivers are
Hercules 720×348, VGA 360×480, EGA 640×350, and planar SVGA 800×600.

Each choice uses its original visible map dimensions and native map-cell size,
integer-rounded viewport geometry, driver palette limits, title backdrop, and
the matching Hercules/CGA/planar/chunky dungeon-wall treatment. The expanded
map also follows the original driver: 320-pixel modes show three separately
acknowledged 37-row thirds, while wider drivers show the complete map at their
native 3-, 4-, 5-, or 7-pixel cell scale. Wilderness projection likewise uses
each branch's original horizon, horizontal/depth steps, height multiplier, and
16- or 256-color palette path. Native pixels are enlarged with nearest-neighbor
sampling, non-square-pixel modes are shown in corrected 4:3 CRT shape, and
Hercules uses ordered monochrome dithering.

The selected mode is saved in the local `MWPORT.CFG` beside the executable.
On a new installation, option **A** (1024×768, 256-color chipset driver) is
selected by default.
Press `Alt+V` from character selection or during dungeon exploration to
change it again. The SDL window remains resizable in every mode.

## Classic and Enhanced experiences

New characters can select:

- **Classic** — the original 251-floor scale and original progression limits.
- **Enhanced** — an extended 1,000-floor dungeon with additional enemies,
  bosses, equipment, spells, magic items, relics, late-game quests, two new
  races (Dragonkin and Celestial), and two new classes (Spellblade and
  Paladin). Player progression can continue through level 3,000.

The Enhanced additions do not replace Steve Moraff's original content. They
extend the native port's optional Enhanced mode.

## Colosseum mode

Press `Tab` on character selection to open a second page containing ten
dedicated Colosseum saves. This Enhanced-only roguelike side game builds a new
combatant through randomized enemy rounds, champion fights, rarity-scaled
weapons, armor, magic, healing, and permanent run perks. Its
`COLOSSEUM0.SAV`-`COLOSSEUM9.SAV` records are isolated from all ordinary
adventure characters and world state. See `COLOSSEUM.md` for the full rules.
On either save page, press `D`, choose `0`-`9` (or click the desired row), and
confirm with `Y` to permanently delete that save. Adventure deletion includes
its map, monster, pitfall, and Beastiary sidecars; Colosseum deletion removes
only the selected Colosseum record.

## Useful documentation

- `EXPERIENCE_MODES.md` — Classic versus Enhanced behavior
- `DEEP_DUNGEON.md` — Enhanced floors, bosses, equipment, and relics
- `DEEP_SPELLS.md` — Enhanced spell catalog
- `COLOSSEUM.md` — separate-save roguelike arena mode
- `HOTKEYS.md` — controls and optional diagnostic shortcuts
- `CREDITS.md` — original-game, port, and preservation credits
- `THIRD_PARTY_NOTICES.md` — SDL and build-tool acknowledgements
- `LICENSE_PORT.txt` — license for the port code only

## Saves and local files

The port uses only files beside its executable. Character saves, generated
dungeons, monster state, bestiary progress, display settings, and other
runtime data are created in the game directory. Do not install it under a
location where your account cannot write files.

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
