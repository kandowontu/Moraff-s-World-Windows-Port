# WORLD.C / WORLD.ASM port status

This is the source-level coverage ledger for the native 1024x768 port. It is
intended to answer two different questions without conflating them:

1. Which original game behaviors have a native implementation?
2. Which decompiled functions still contain behavior that has not been ported?

The mapping is behavioral, not instruction-for-instruction. The original DOS
program often spreads one feature over many tiny functions, while the native
port usually implements the same feature in one data-driven subsystem.

## Status legend

- `[x]` Implemented in the native port.
- `[~]` Partially implemented; the missing portion is stated explicitly.
- `[ ]` Not implemented.
- `[=]` Replaced by the native platform layer; not gameplay backlog.
- `[+]` Native extension; no `WORLD.EXE` counterpart.

Source comments use the corresponding tags `MW_PORT`,
`MW_PLATFORM_REPLACEMENT`, and `MW_EXTENSION`.

## Implemented original routines

### Startup, character data, and progression

| Status | Original routine(s) | Native implementation |
|---|---|---|
| `[x]` | `select_player` `0x0889F`, `character_menu` `0x092B4` | `mw_game.c`: `player_select_screen`, slot load/save UI; native 16-bit enchant/quest extension auto-imports legacy records |
| `[x]` | `func_037B5`, `func_19115` | Complete `ROLL.TXT` character-creation flow, race rolls, rerolls, name, sex and class selection |
| `[x]` | `func_0A4CF`, `func_0A51B`, `func_0A548`, `func_0A60D` | Packed 0x928-byte character save/load in `game_load_character` / `game_save_character` |
| `[x]` | `inn_service`, `func_0A6F2`, `func_0A751` | Experience thresholds, inn rest, level gains, age, HP/SP recovery |
| `[x]` | Dungeon portions of `func_0F5CD`, `func_0F6E5` | `game_init`, `game_run`, status, keyboard command dispatch, movement, save/quit, death and raise contract |

### Dungeon generation, state, movement, and map

| Status | Original routine(s) | Native implementation |
|---|---|---|
| `[x]` | `load_dungeon_bin` `0x0A3E7` | `game_load_dungeon` reads local `DUNG.BIN`/`WORLDMAP.BIN` |
| `[x]` | `func_1EE04`, `calc_damage`/`far_1EFA4`, `func_1F2D4` | Exact template hash, two-bit edge lookup, and rock-cell lookup |
| `[x]` | `func_1EEC9` | Original coordinate-hash ladder placement and direction |
| `[x]` | `func_0C83D`, `func_0F2A8`, `func_0F30A`, `trap_door` | Surface locations, ladders, keyed trap doors and floor changes |
| `[x]` | `func_0EA5A`, `func_0E913` | Hidden-pit generation, discovery persistence, interruption and fall flow |
| `[x]` | `func_0C970`, `func_0EAE9` | Monster adjacency, door-aware visibility, encounter blocking and auto-engagement |
| `[x]` | Secret-door and occupied-door branches inside `func_0F6E5` | Edge 1 visible doors and edge 2 wall-like secret doors both auto-open when empty, stop revelation, hide monsters and show their distinct `JAMMED` messages when occupied |
| `[x]` | `func_1F077`, `func_1F3FD`, `far_1FAE6` | Remembered/revealed map, doors, ladders, trap doors, used pits, shops and blinking player marker |
| `[x]` | `func_0EDAD` | Dig command, wall/floor traversal, messages, timing and landing checks |
| `[x]` | Relocate/ascend/descend/pass-wall branches of the spell helpers and `func_0F30A` | `game_relocate`, `game_change_floor`, `game_pass_wall` |

### Dungeon rendering and user interface

| Status | Original routine(s) | Native implementation |
|---|---|---|
| `[x]` | `func_14B85`, `func_14F53`, `func_16488` | PIC actors, textured wall faces, doors and perspective scene renderer |
| `[x]` | `func_1F355`, `func_1F3B2`, `func_1F9EF` | Dungeon geometry and projection helpers |
| `[x]` | `func_0D74F` | Four simultaneous 1024-mode viewports |
| `[x]` | `func_27112` | Command legend; the native version also gives every legend item a mouse hit box |
| `[x]` | `use_item` `0x0F4E7` | Original expanded-map `GO WEST/EAST/NORTH/SOUTH` hint toward live quest records `0x68..0x6F` |
| `[x]` | `examine_item` `0x0E3C8`, `func_0E578` | Original ZOOM chooser, arrow/mouse compass selection, full-screen directional viewport, adjacent-monster label and three view-size modes |
| `[x]` | `func_26C03` and help-text dispatch | Four help categories plus spell help pages |
| `[x]` | `func_0C031` | Both spells-in-effect pages |
| `[x]` | `func_0DF4A`, `func_0DBA5`, `func_0DBE8` | Statistics and status displays |
| `[x]` | `shop_finances` `0x0803D` | Money and stone display |
| `[x]` | `func_0DDAA` | Pockets, spells, scrolls, wands, papers, pills and misc-item inventory pages |
| `[x]` | Expand-map and zoom branches of `func_0F6E5` | Expanded map plus the original modal directional zoom flow; pressing Z again in the chooser cycles the three four-view sizes |
| `[x]` | `func_1D5A7` | Rotating floor-zero tutorial plus damage, poison, disease, level-ready, spell-point and carried-weight advice in the upper-left pane |

### Wilderness engine

| Status | Original routine(s) | Native implementation |
|---|---|---|
| `[x]` | `func_1A765`, `func_1A786`, `func_1A841`, `func_1ADB1` | Original 64x64 `WORLDMAP.BIN` anchors, exact runtime LCG-seeded midpoint displacement, signed land/water heights, cached 257x129 regional pages and wrapped coordinates |
| `[x]` | `func_1B137`, `func_1B169`, `func_1B504`, `func_1B5DB`, `func_1BC5B`, `func_1C232`, `func_1C571`, `func_1C732` | The original 1024x768x256 `func_1B5DB` 3x4x6 wireframe projection and literal 256-entry DAC ramp; lower-resolution chipset variants converge on that native output |
| `[x]` | `func_1C8D2`, `func_1C92C`, `func_1CB3D` | Height-projected outdoor player/boat and generated-dungeon markers plus the projected complete-world overview |
| `[x]` | `func_1CCB5` | Outdoor movement and edge wrapping, boat purchase/abandon rules, regional dungeon placement, `E` entry and clean floor-zero dungeon generation |

The original advice explicitly says monsters are only found in the dungeon;
therefore the absence of wilderness encounters is preserved behavior, not a
missing subsystem.

### Monster generation, combat, magic, and rewards

| Status | Original routine(s) | Native implementation |
|---|---|---|
| `[x]` | `func_09185`, `func_091B1`, `func_091CD` | Monster records, floor eligibility, ordinary/boss separation and spawn selection |
| `[x]` | `load_monster_map`, `func_09DA6`, `func_09E0D`, `func_09E73`, `func_09EA8`, `func_0A03B`, `func_0A20C`, `func_0A318`, `func_0A36A`, `func_0A39C` | Persistent monster floors, deaths, HP and pit/world history |
| `[x]` | `func_09148`, `combat_encounter` `0x018FE` | Viewport encounter setup and fight loop |
| `[x]` | `combat_attack` `0x00AFB`, `func_0A7FF`, `func_0AC4F`, `func_0AD33`, `func_0AD6C` | Player and monster attack resolution, armor, weapon and damage formulas |
| `[x]` | Combat helpers `0x00D04..0x0446B` | Monster misses, physical attacks, breaths, poison, disease, level/stat drains and status attacks |
| `[x]` | `select_weapon` `0x04538` and its inventory helpers | Weapon selection, owned/equipped state and enchantments |
| `[x]` | `spell_menu` `0x00436`, `combat_event` `0x005DB`, `cast_spell` `0x0079A` | Unified spell/scroll/wand/paper selector and casting dispatch |
| `[x]` | `func_10E9A`, `func_10EC6`, `func_10EF2`, `func_10F1E`, `func_10FE5`, `func_110AC`, `func_110CE`, `func_110F0`, `func_11112`, `func_11134`, `func_11156` | Permanent/preparation spell effects and selection helpers |
| `[x]` | `func_1158A`, `func_115B6`, `func_115E2`, `func_116CA`, `func_11753`, `func_1177D`, `func_117A7` | Battle spell setup, buffs, resistances, healing and monster effects |
| `[x]` | `weapon_glow`, `func_11876`, `func_118DC`, `func_119D5`, `func_11AEC`, `func_11B18`, `weapon_effect`, `func_11BE0`, `func_11C16`, `func_11C4C`, `func_11C82`, `func_11CB8`, `func_11CEE`, `func_11DA5` | Weapon spell visuals/effects, damage spells, cures, holds, drains and expiration state |
| `[x]` | `func_0CA5F`, `func_0CDDD`, `func_0D2C9` | Turn expiration, battle cleanup and town cleanup of effects |
| `[x]` | Kill/reward chain through `func_21F9C` | Defeat message, XP, pill/key/armor stages, ordered stone choice, misc magic, spell items, boss rewards and persistent kill |

### Shops and inventory

| Status | Original routine(s) | Native implementation |
|---|---|---|
| `[x]` | `shop_buy_check`, `shop_weapon`, `shop_pills`, `shop_magic`, `shop_misc` | Store purchase checks and all item categories |
| `[x]` | `shop_main_menu`, `shop_buy_item` | Store menu and purchase flow in the upper-left window |
| `[x]` | `func_081C1`, `func_08326` | Inn/temple/bank location dispatch and finance operations |
| `[x]` | `inn_hotel`, `inn_full` | Inn/hotel services, recovery, non-payment result and town cleanup |
| `[x]` | `drop_item_menu`/`func_0C366` | Lose-item flow for armor, weapons and spell items |
| `[x]` | Consumable portion of `use_item` | Pills, grenade, seeing stone, teleport stone, floor slosher, potion, scroll, wand and paper mechanics |

## Completed original mouse-control layer

Earlier decompiler comments incorrectly described this as two-player support.
The `two_player_mode` global is actually set after DOS INT 33h mouse-driver
detection, and the supposed player-switch helpers manage mouse buttons and the
software cursor.

- `[=]` `func_2635C`, `func_26394`, the routine mislabeled
  `set_active_player` `0x263D4`, and `func_26411` are replaced by SDL mouse
  detection, button events, coordinates, and window bounds.
- `[=]` `func_26A21`, `func_26AB9`, `flush_gfx_buffer` `0x26AD5`, and the
  routines mislabeled `swap_player_gfx` `0x26B78` / `toggle_player_gfx`
  `0x26BD2` restore or synchronize the DOS software cursor. The OS/SDL cursor
  does not require those backing-pixel operations.
- `[x]` `func_26597` rectangle hit-testing has native logical-coordinate hit
  maps for all four direction viewports and the complete command legend.
- `[x]` Native mouse selection is active in character slots/creation, town and
  shop services, rewards, pockets, equipment, combat, bestiary, spell-source
  menus, the original 30-spell grid, trainer fields, wilderness movement,
  numeric amounts and character-name entry.
- `[x]` `func_08EA2`, `select_player`, `character_menu`, `func_0E578`,
  `func_0F6E5`, `check_key`, and `func_26B38` behavior is covered by click hit
  maps, continuous hover feedback, DOS extended-key draining and SDL cursor
  synchronization.

## Remaining original gameplay functions

No known single-player gameplay routine remains wholly unimplemented in the
current source audit. `func_0F6E5` now includes audible movement, door, ladder,
fall, combat, spell, money and error events behind O, plus visible incremental
map-brick timing behind all four B settings. The queued square-wave phrases are
a native PC-speaker-style replacement because the disassembly exposes event
state but no reusable sampled sound assets.

### Final hidden-path audit (2026-07-21)

This pass rebuilt the inventory from the disassemblies rather than accepting
the tables above as proof of coverage. It checked all 493 marked `WORLD.C`
routines, all 1,296 recovered string records, and all 50 `jmp [cs:...]`
computed-dispatch sites in `WORLD.ASM`. Indirect compiler/runtime and graphics
driver calls were classified separately from gameplay dispatch.

The fresh audit found and corrected four live paths that the earlier ledger
had overclaimed:

- edge value 2 is an auto-opening **secret door**, not solid stone;
- monsters behind either visible or secret doors hide and jam the doorway;
- `func_0E578` is a modal directional full-screen zoom, not merely a layout
  toggle;
- Escape in the dungeon clears/dismisses the transient pane and does not quit
  without saving; Q remains save-and-quit.

It also restored the original no-monster Fight response, no-trap-door K
response, wall/door/secret-door movement messages, and mode-aware mouse hit
boxes after changing view size. After those corrections, the remaining
computed tables resolve to implemented combat attacks, spell grids/effects,
shop/reward selections, monster/boss rewards, renderer cases, or native
platform replacements listed below. No additional dormant gameplay command or
wholly absent single-player subsystem was found.

## Platform replacements (not gameplay backlog)

The following original routines were hardware/compiler services. Recreating
their DOS implementation would be wrong for a native SDL port; their visible
results are supplied by `mw_video.c`, `mw_input.c`, standard C I/O, or the
native launcher.

- `[=]` Text/font/image primitives `func_23FA6..func_2473D`.
- `[=]` Palette construction `func_2478E` and related DAC helpers; the native
  version preserves the floor-dependent 1024-mode palette.
- `[=]` Drawing primitives `func_2535D..func_25943`.
- `[=]` DOS keyboard polling part of `check_key`/`func_26B38`, plus INT 33h
  mouse detection and software-cursor drawing. Original click mappings that
  trigger gameplay remain in the partial backlog above.
- `[=]` BGI/VGA bank, chipset and graphics-runtime code
  `func_28066..func_2B98A`.
- `[=]` DOS file/path glue from `build_filepath` `0x277E7` through
  `build_open_file` `0x2792F`; native files are local and relative to the
  executable directory.
- `[=]` Original copy-protection check `func_08BDF`; it is replaced with
  SHA-256 verification of the required local `MW.EXE` and `WORLD.EXE`.
- `[=]` Non-1024 video modes. This port intentionally targets the original
  1024x768x256 presentation requested for the project.

`show_game_info` `0x08C53` is intentionally suppressed, not accidentally
missing: the native GUI executable starts directly in the game without the
old loading/information window.

## Native extensions

- `[+]` Bestiary, discovery persistence, kill counts and full-screen monster
  image view.
- `[+]` Ctrl+F11 non-persistent wilderness test sandbox.
- `[+]` Ctrl+F12 trainer recreation.
- `[+]` Ctrl+F6 through Ctrl+F10 dungeon reroll, open-floor, town teleport,
  god-mode and noclip testing controls.
- `[+]` G-key derived save-statistics report and Beastiary Ctrl+F12 catalog
  unlock.
- `[+]` Mouse-selectable trainer fields, spell grids, equipment and printed
  footer commands.
- `[+]` SHA-256 ownership/integrity gate for the original executables.
- `[+]` Self-tests and screenshot/test harnesses in `main.c`.

## Updating this ledger

When porting another original routine:

1. Add an `MW_PORT: WORLD ...` comment above the native subsystem or function.
2. Move the routine from the unported/partial section to the implemented table.
3. State whether the result is exact, behavioral, or a platform replacement.
4. Add or extend a self-test when the behavior can be checked without playing
   through a save.
