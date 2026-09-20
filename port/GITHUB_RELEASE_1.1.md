# Moraff's World Native Port 1.1.2

Version 1.1.2 adds the optional native Moraff's Revenge Advanced 3.3
reconstruction and a strict, source-backed parity gate while retaining the
World and Colosseum improvements from earlier 1.1 releases.

## Highlights

- integrates Moraff's Revenge as an optional, separate-save bonus game when
  users supply the legally obtained original Revenge 3.3 data
- publishes an honest routine-level Revenge audit: 97 verified routines, 10
  documented adaptations, and 60 unresolved routines; full 1:1 parity is not
  yet certified
- fixes Revenge combat-contact dispatch, modal combat input, poll ordering,
  incremental viewport updates, combat-sprite XOR behavior, ladder and
  false-floor notices, and false-floor detection

- keeps damage and counterattack results visible until the next deliberate
  command in both Adventure and Colosseum play, matching `WORLD`'s original
  retained-pane and buffered-key behavior
- holds every terminal Colosseum exchange until a fresh acknowledgement;
  finishing blows, lethal counterattacks, and escape/flee results can no longer
  be auto-dismissed by held input before the next screen
- preserves original held-F attacking through the DOS-compatible 500 ms
  initial delay and 92 ms typematic repeat cadence
- completes another `WORLD.ASM`/`WORLD.C` behavior audit, including original
  hold/repeat timing, keypad semantics, fight controls, modal item/spell flow,
  title and death behavior, wilderness travel, and dungeon interactions
- expands Enhanced play to 1,000 floors and player level 3,000
- adds two Enhanced races, two Enhanced classes, eight late weapon/armor
  tiers, 60 deep spells with matching magic items, additional monster variants,
  milestone bosses, and rare relic effects
- adds a separate ten-save Colosseum roguelike mode with randomized battles,
  reward drafts, perks, healing, champion rounds, and career statistics
- adds a live-formula battle simulator with direct numeric entry and
  10,000-trial result analysis
- expands the trainer, Beastiary, model/palette viewer, mouse interaction,
  diagnostics, and in-package documentation
- recreates all twelve original display-driver branches with their native
  resolutions, palette restrictions, dungeon and door renderers, map scales,
  wilderness projections, and title treatments; option A is the default
- keeps the Colosseum opening approachable while accelerating enemy level and
  endurance later in a run so strong builds do not flatten the long game

Download and extract the Windows x64 ZIP, then supply the ten required files
from your own legally obtained Moraff's World installation. Original
MoraffWare executables and assets are **not included**. The exact required file
list and approved executable checksums are in `ORIGINAL_FILES_REQUIRED.md`.

## Release SHA-256

```text
f7f21cc30fbc20ec540c0b275d7f8b4a4d89f7df41719dcfe7f0a195ee5a35c1  moraffs_world.exe
49fc6982720872da32b57a569c3abb14901a9738aa98132e5f3b02ac660809b4  Moraffs-World-Native-Port-1.1.2-win64.zip
```

The same values are attached as
`Moraffs-World-Native-Port-1.1.2-win64-SHA256.txt`.

Moraff's World and its original assets remain the work and property of Steve
Moraff / MoraffWare. This independent fan preservation project is not
affiliated with or endorsed by MoraffWare.
