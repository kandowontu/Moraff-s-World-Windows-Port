# Moraff's World Native Port 1.1.01

Version 1.1.01 corrects combat-feedback timing in the native Windows port and
retains the complete fidelity and Colosseum work from 1.1.1.

## Highlights

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
be1491d4c04315b349025b43165cd761cdf10cb1af5093a9a6accadcbf11f6ee  moraffs_world.exe
9eaf494fbf7678a5f9a3d07906c3593dbbce74495aea006f58ba06ddc7fa8ba8  Moraffs-World-Native-Port-1.1.01-win64.zip
```

The same values are attached as
`Moraffs-World-Native-Port-1.1.01-win64-SHA256.txt`.

Moraff's World and its original assets remain the work and property of Steve
Moraff / MoraffWare. This independent fan preservation project is not
affiliated with or endorsed by MoraffWare.
