# Colosseum mode

The Colosseum is an optional, Enhanced-only roguelike combat mode. From the
character-selection screen, press `Tab` to switch between **Adventure Saves**
and **Colosseum Saves**. The Colosseum page supplies ten additional slots.

Colosseum records are completely separate files named `COLOSSEUM0.SAV`
through `COLOSSEUM9.SAV`. The mode never loads, changes, or overwrites the
ordinary adventure character files `0` through `9`, their dungeon files,
monster maps, pitfall history, or Beastiary records.

From either character-selection page, press `D`, choose a slot with `0`-`9`
or the mouse, then confirm with `Y` to permanently delete it. Deleting a
Colosseum slot removes only its separate `COLOSSEUM#.SAV` record.

## A run

- Each new combatant uses the Enhanced race, class, equipment, and magic
  catalogs.
- A new run selects one of three difficulties. **Easy** lowers enemy level,
  HP, and attack pressure, restores 15% HP/8% SP after ordinary victories and
  has 15%/25% skip costs. **Normal** uses the intended balance, restores
  10% HP/5% SP, and has 20%/35% skip costs. **Hard** raises enemy level, HP,
  and attack pressure, restores 7% HP/3% SP, has 25%/45% skip costs, and gives
  a modest improvement to reward-rarity rolls. The two skip values are for an
  ordinary challenger and champion respectively.
- Rounds 1-10 use the original onboarding threat ladder. After the first
  champion, enemy level accelerates gradually with round, with a second
  long-run growth band after round 80; it does not simply copy the player's
  current build. Normal challengers vary around 90-115% of that threat and
  champions around 110-130%, with Easy/Hard shifting those bands. Arena
  opponents roll in the upper portion of their native HP range and gain an
  additional endurance band after round 25 so late equipment, enchantments,
  and stacking perks do not make later encounters progressively trivial.
- Victories normally grant one player level and one random core-stat point.
  Champion victories add one level and two extra stat rolls. The Sage Prism
  grants one additional level on each fifth victory. HP/SP growth and ordinary
  post-fight recovery are deliberately bounded to prevent exponential builds.
  Every fifth victory cures poison/disease and restores an additional 45%,
  35%, or 25% HP/SP on Easy, Normal, or Hard respectively.
- Every tenth round is a champion battle. Champion rewards are promoted by at
  least one rarity tier and the victory grants two freshly rolled reward
  drafts instead of one.
- Winning presents seven persisted choices: a weapon, armor, learned spell,
  scroll, wand, paper, and boon. Leaving before choosing does not reroll the
  choices. The four magic cards never repeat the same named spell within one
  draft, even when that spell exists in both Wizard and Priest traditions.
- Reward rarity runs from Common through Super Ultra Rare. Mundane gear
  develops throughout the opening rounds, the first Enhanced tier enters at
  round 20, and the final common tier is not reached until round 108. Higher
  rarities can jump up to four tiers ahead, subject to a power-ratio ceiling
  that prevents a single lucky early draft from being dozens of times
  stronger than its current band. Permanent enchantments use a small,
  round-scaled budget because each 40 accuracy points creates another complete
  attack swing in the original combat formula.
- Yellow **Super Ultra Rare** weapon and armor cards are true jackpots: they
  roll three to five ladder tiers ahead and may cross the ordinary gear
  power-ratio gate. They are intentionally capable of awarding equipment a
  few stages before its normal round band.
- Spell rewards advance about one level every three rounds through the
  opening catalog, then use increasingly wider gates for Enhanced magic. A
  rare reward can jump at most three spell levels ahead, so five-digit deep
  attacks cannot routinely appear in the teens. Magic rarity is determined by
  the spell's actual level rather than by its delivery source: the same spell
  cannot be Common when learned and Rare when offered on a scroll.
- Magic rewards contain only Wizard/Priest battle spells: damage, hostile
  status effects, and battle healing. Permanent and preparation magic,
  Relocate, Pass Wall, Go Away, traversal, and other exploration effects are
  stripped from new and existing Colosseum saves. Eligible spells may still
  arrive learned or as a scroll, wand, or paper.
- Boons include healing, spell-point recovery, permanent HP/SP growth, stat
  growth, protection, regeneration, battle power, and rare relics. Every boon
  card states its exact numeric effect; relic cards identify the relic and its
  arena effect before selection. Permanent percentage growth, stat gains,
  armor, Fury, and regeneration use bounded values; regeneration caps at ten
  HP per living action.
- Older Colosseum records are migrated automatically. Career records, the
  active run, build, round, streak, and unclaimed draft remain. Version 9
  replaces only an active opponent generated by the former flattened
  late-run level curve so resuming a save immediately uses the new scaling.
- Enemy level drain is capped at one level per successful drain in Colosseum
  mode. Adventure monsters retain their original/full drain values.
- The original Ball/orb and Puffball palette families are excluded from
  Colosseum opponents. Their extreme endurance or non-damaging special turns
  make them dungeon flavor rather than useful arena challenges; both remain
  unchanged in Adventure mode and the Beastiary. Saves already fighting one
  automatically replace that encounter when loaded by the corrected build.
- Ordinary melee receives a modest Colosseum-only level bonus, while early
  damage spells receive a level/mental-stat floor so spending SP or an item is
  meaningfully stronger than a free attack. Fixed battle cures scale with
  maximum HP in Colosseum mode. Incoming damage protection now tapers through
  round 20 rather than disappearing at the first champion.
- `K` can skip the current challenger. It forfeits the reward, breaks the
  current streak, advances the round, clears battle-only effects, and applies
  the difficulty-specific HP/SP cost, never reducing HP below one.
- Death never deletes the Colosseum save. It immediately asks whether to
  restart. Choosing Yes selects a new difficulty and resets the character to
  their original level, attributes, HP/SP, inventory, spells, equipment,
  effects, perks, and round-one state. Career victories, deaths, best streak,
  designed race/class, and slot remain. Choosing No returns to the save page;
  selecting the ended slot offers the same restart flow later.
- Retirement also ends only the current build; it does not delete the slot or
  its career record.
- The current enemy, HP, effects, build, and unchosen reward cards are saved
  after every meaningful action.

## Controls

| Key | Action |
| --- | --- |
| `Ctrl+F1` | Toggle Turbo Mode; while enabled, `+`/`-` changes timing from 25%-1000% in 25% steps. Turning it off restores 100%. |
| `F` | Fight |
| `C` | Cast battle magic |
| `I` | Use a magic item |
| `W` / `A` | Select weapon / armor |
| `T` | Wait one combat turn |
| `K` | Confirm skipping the battle for its HP/SP and streak penalty |
| `V` | View the current run and perk sheet |
| `O` | Toggle Colosseum sound on/off; the setting is saved with that Colosseum slot |
| `H` | View Colosseum help |
| `S` | Save immediately |
| `Q` or `Escape` | Confirm, save, and return to character selection |
| `X` | Confirm retirement of the current run |
