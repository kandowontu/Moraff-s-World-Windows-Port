# Colosseum mode

The Colosseum is an optional, Enhanced-only roguelike combat mode. From the
character-selection screen, press `Tab` to switch between **Adventure Saves**
and **Colosseum Saves**. The Colosseum page supplies ten additional slots.

Colosseum records are completely separate files named `COLOSSEUM0.SAV`
through `COLOSSEUM9.SAV`. The mode never loads, changes, or overwrites the
ordinary adventure character files `0` through `9`, their dungeon files,
monster maps, pitfall history, or Beastiary records.

## A run

- Each new combatant uses the Enhanced race, class, equipment, and magic
  catalogs.
- Every round selects a random monster from a level range appropriate to the
  combatant's level and current streak.
- Every tenth round is a champion battle.
- Winning presents four persisted choices: a weapon, armor, magic reward, or
  boon. Leaving before choosing does not reroll the choices.
- Reward rarity runs from Common through Super Ultra Rare. Higher rarities can
  jump much farther along the gear or spell ladder than the current round.
- Boons include healing, spell-point recovery, permanent HP/SP growth, stat
  growth, protection, regeneration, battle power, and rare relics.
- Death or retirement ends the current build, but career victories, deaths,
  and best streak remain in that Colosseum slot. The next visit can start a
  fresh randomized run with the same designed combatant.
- The current enemy, HP, effects, build, and unchosen reward cards are saved
  after every meaningful action.

## Controls

| Key | Action |
| --- | --- |
| `F` | Fight |
| `C` | Cast battle magic |
| `I` | Use a magic item |
| `W` / `A` | Select weapon / armor |
| `T` | Wait one combat turn |
| `V` | View the current run and perk sheet |
| `H` | View Colosseum help |
| `S` | Save immediately |
| `Q` or `Escape` | Confirm, save, and return to character selection |
| `X` | Confirm retirement of the current run |
