# Moraff's World Native Port 1.1.3

Version 1.1.3 bundles the optional native Moraff's Revenge Advanced 3.3
runtime data and retains the strict, source-backed parity gate plus the World
and Colosseum improvements from earlier 1.1 releases.

## Highlights

- integrates Moraff's Revenge as an optional, separate-save bonus game with
  its required 79 KB runtime-data subset included—no manual data copy needed
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
from your own legally obtained Moraff's World installation. Original Moraff's
World executables and assets are **not included**. The exact required World
file list and approved executable checksums are in
`ORIGINAL_FILES_REQUIRED.md`. The minimal Revenge runtime-data subset is
included and remains original MoraffWare material.

## Release SHA-256

```text
159c3962adc6525bf142b23bffa7fc01c06ee031655192fd38bfb2c19b15231e  moraffs_world.exe
ff7511aa5f9c2d58bdd5e7f8b2c1c812dcc448cae529b8386e2768fd627c1076  Moraffs-World-Native-Port-1.1.3-win64.zip
```

The same values are attached as
`Moraffs-World-Native-Port-1.1.3-win64-SHA256.txt`.

Moraff's World and its original assets remain the work and property of Steve
Moraff / MoraffWare. This independent fan preservation project is not
affiliated with or endorsed by MoraffWare.
