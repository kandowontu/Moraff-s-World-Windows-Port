# Release notes — Version 1.1.3

Released September 19, 2026.

## Moraff's World Native Windows Edition

This release packages the native Windows port, documentation, and bundled
Moraff's Revenge runtime data. Users supply the required original Moraff's
World files from their own installation.

Highlights include:

- The 36-file, 79,129-byte Moraff's Revenge runtime-data subset is now bundled
  with the Windows package and source tree. Users no longer need a separate
  Revenge installation or manual `revenge` directory setup. Mutable Revenge
  characters and world state remain isolated in `revenge-native`.

- The optional native Moraff's Revenge Advanced 3.3 reconstruction is now
  integrated into the character-selection flow. It uses a separate local
  `revenge` data directory and never reads or modifies World or Colosseum
  saves.
- A new source-backed parity gate inventories all 167 recovered Revenge
  routines. Version 1.1.3 has 97 verified routines, 10 documented native
  adaptations, and 60 unresolved routines, so the bonus game is explicitly
  shipped as an audited work in progress rather than described as fully 1:1
  certified.
- The latest Revenge fixes correct combat-contact ownership, modal combat
  input, exploration/combat random polling order, incremental viewport
  presentation, central-monster XOR handling, ladder and false-floor notices,
  and the exact two-level false-floor predicate.
- Seventeen native Revenge regression suites cover data, world resources,
  saves, character formulas, spells, items, monsters, combat, loot,
  progression, town, input, session flow, frontend flow, assets, rendering,
  and the native game bridge.

- Adventure and Colosseum damage/counterattack readouts now reproduce
  `WORLD`'s original retained-pane behavior: an ordinary result stays visible
  until the next deliberate command, already-buffered keys are drained, and
  held-F attacks continue at the original DOS typematic cadence; every terminal
  Colosseum exchange now requires a fresh acknowledgement after the command
  key is released, preventing finishing blows, lethal enemy counterattacks,
  escape, or fleeing results from being replaced by the next screen
- All twelve original `MW.EXE` display-driver choices now use their own native
  framebuffer dimensions, palette limits, dungeon-wall path, map scale,
  wilderness projection, title treatment, and door geometry; option A
  (1024×768 chipset 256-color) is the clean-install default
- Colosseum balance now preserves its opening rounds while accelerating enemy
  level after round 10 and again after round 80, with additional late-run
  endurance scaling and automatic migration of active version-8 encounters
- Startup validation now recognizes both approved original `WORLD.EXE`
  distribution variants using their exact size, CRC-32, and SHA-256 values
- A source-guided fidelity audit of original input, hold/repeat timing,
  keypad semantics, combat restrictions, item dialogs, title/death flow,
  wilderness travel, doors, ladders, traps, shops, treasure, and magic
- Native 64-bit Windows executable and 1024×768 game layout
- Four original-style directional viewports and progressive dungeon map
- Classic 251-floor experience
- Optional Enhanced 1,000-floor experience
- Enhanced player progression through level 3,000, two additional races, two
  additional classes, eight late-game weapon and armor tiers, 60 deep spells,
  matching scrolls/wands/papers, new monster variants, and milestone bosses
- Ten-slot Enhanced Colosseum side mode with completely isolated save files,
  randomized level-appropriate challengers, champion rounds, rarity-scaled
  reward drafts, healing and persistent run/career records
- Original and Enhanced monsters, combat, magic, equipment, shops, treasure,
  status effects, character creation, wilderness, and boats
- Original black, royal-blue, and charcoal 1024×768 title backdrop, captured
  title DAC colors, cumulative monster-pop introduction, and complete original
  credit card, with selected Enhanced monsters added to the showcase lineup
- Death and in-game save/quit return to the title screen; only Esc/Q from the
  title flow (or closing the window) exits the application
- Rebalanced Enhanced recovery magic: Life Convergence replaces Mass
  Restoration with a damage-to-healing combat spell, Soul Anchor replaces
  Full Restoration with a one-use resurrection bind, and Phoenix Prayer no
  longer restores spell points
- Bestiary, native trainer, game statistics, grouped model viewer with every
  used monster recolor/tint and wall-palette variant, an edge-to-edge
  full-screen inspection mode, mouse controls, and documented optional
  diagnostic hotkeys
- Native `Ctrl+F2` battle simulator using the live melee and spell formulas,
  direct numeric entry, 10,000-trial result summaries, breakdowns, and damage
  distributions
- Local relative-path runtime with no installer or registry dependency
- Verification of the required original `MW.EXE` and `WORLD.EXE`

This release does not include original Moraff's World executables or assets.
It does include the minimal original Moraff's Revenge runtime-data subset used
by the optional native bonus game; those files remain MoraffWare property. See
`ORIGINAL_FILES_REQUIRED.md` for the World requirements.
