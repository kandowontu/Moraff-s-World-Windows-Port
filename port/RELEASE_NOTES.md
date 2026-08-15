# Release notes — Version 1.1.1

Released August 15, 2026.

## Moraff's World Native Windows Edition

This release packages the native Windows port as a legally clean executable
and documentation bundle. Users supply the required original MoraffWare files
from their own installation.

Highlights include:

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
See `ORIGINAL_FILES_REQUIRED.md`.
