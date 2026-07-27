# Native deep-dungeon extension

The native port supports dungeon floors 0 through 1000. Floors 251-1000 use
the same deterministic wall, door, ladder, pitfall, palette, fog-of-war, and
four-viewport systems as the earlier dungeon, but have their own escalating
monster progression.

## Deep monster generations

Sixty native monsters reuse original `WORLD.PIC` silhouettes with new palette
families. Their colors are remapped at draw time; the original assets and the
monsters on floors 1-250 remain exact.

| Floors | Monsters |
| --- | --- |
| 251-325 | Azure Ogre, Crimson Werewolf, Jade Swordwraith, Ashen Kobald |
| 301-375 | Violet Orc Warden, Emerald Deep Dwarf, Sapphire War Knight, Blood Ape |
| 351-425 | Violet Unicorn, Storm Titan, Abyss Giant, Golden Devourer |
| 401-475 | Crimson Death Mask, Frost Skeleton, Plague Zombie, Violet Wraith |
| 451-500 | Obsidian Mummy, Astral Vampire, Blood Medusa, Void Demon |
| 501-575 | Cobalt Gargoyle, Ash Titan, Venom Unicorn, Scarlet Dragonkin |
| 551-625 | Runic Stone Lord, Umbral Devourer, Glacial Vampire, Ember Medusa |
| 601-675 | Crystal War Golem, Plague Wyrm, Storm Reaper, Void Centaur |
| 651-725 | Molten Ogre, Spectral Werewolf, Obsidian Knight, Emerald Hydra |
| 701-775 | Astral Mummy Lord, Crimson Lich, Sapphire Demon, Golden Behemoth |
| 751-825 | Rift Stalker, Frost Ape, Jade Death Mask, Solar Wraith |
| 801-875 | Ebon Titan, Prismatic Basilisk, Blood Warlock, Celestial Giant |
| 851-925 | Abyssal Dragon, Chrono Knight, Viridian Reaver, Starlight Medusa |
| 901-975 | Void Colossus, Scarlet Vampire Lord, Storm Demon, Crystal Doom |
| 951-1000 | Eternity Wraith, Radiant Titan, Umbral Dragonking, Chaos Incarnate |

Adjacent generations overlap for 25 floors, so the available roster expands
and contracts as the player descends. Original random monsters stop at floor
250. Previously generated post-250 monster-cache layers are converted to the
correct generation when loaded.

## Vertical-shortcut balance

The DOS game balanced its shortcuts for a 250-floor dungeon: digging stopped
after floor 120, Descend after 123, Ascend/Double Ascend/Major Ascend and Major
Descend after 65, and Major Descend could not land below 75. The 1,000-floor
extension preserves those proportions. Classic retains the original values;
Enhanced uses the scaled values:

| Shortcut | Classic | Enhanced |
| --- | --- | --- |
| Digging | May start through floor 120 | May start through floor 480 |
| Descend | May be cast through floor 123 | May be cast through floor 492 |
| Ascend / Double Ascend / Major Ascend | May be cast through floor 65 | May be cast through floor 260 |
| Major Descend | Cast through floor 65; landing capped at 75 | Cast through floor 260; landing capped at 300 |

The spells retain their literal one-, two-, and twenty-five-floor travel
distances. A blocked learned spell, scroll, wand, or paper reports the relevant
limit without spending spell points or consuming the item. Ladders, trapdoors,
pitfalls, Open Floor Mode, and town/wilderness transitions are unaffected.

Enhanced also adds a separately gated level 11-14 magic catalog. Its 50- and
100-floor traversal spells do not replace or loosen the proportional limits
on the original spells. One entry in every spell family unlocks at each
100-floor milestone. All 40 effects, unlocks, and item-drop rules are
documented in `DEEP_SPELLS.md`.

The deep monsters use the existing resistance and status systems in new
combinations. Depending on the monster, attacks can breathe fire or cold,
inflict poison or disease, dissolve armor with acid, or drain multiple player
levels. Their attack, defense, save, agility, damage, and HP-factor values
continue increasing across fifteen overlapping generation bands.

Magical enemies in every Enhanced generation can replace a melee response
with a signature level 11-14 spell. Early casters make an attempt about once
per four or five responses, later casters about once per three, and milestone
bosses once per two or three. Their versions of percentage-damage spells are
scaled to player health rather than copying the player spell's enormous flat
monster damage. Restoration spells heal the caster, and Mana Tempest also
drains current spell points. The Anti-Magic Ring now has an Enhanced-only
purpose: every plus grants an 8% chance to dispel one of these enemy spells.
The Beastiary lists each monster's spell and casting frequency.

## Widened values

- Dungeon floor IDs and saved monster levels are unsigned 16-bit values.
- Monster definition combat stats are 16-bit values.
- Saved monster HP is an unsigned 32-bit value. Runtime combat remains signed
  32-bit, which is ample for the floor-1000 bosses.
- Enhanced player current and maximum HP use unsigned 32-bit native values,
  matching the scale of the existing spell-point cap. Classic retains its
  original 32,767 trainer cap.
- Permanent weapon/armor enchantments and the armor, weapon, body-armor,
  ring-protection, and gauntlet bonuses use 16-bit native values.
- Player level and the six player attributes were already 16-bit and remain
  so. The trainer permits values through 32767.

The original six-byte `MON.MAP` records are detected and imported
automatically. Native `MWMON002` caches are also imported. The next world-state
save writes `MWMON003`, preserving 16-bit floor/level values and 32-bit HP.
Original character saves are likewise imported automatically; the native
16-bit enchantment data is stored in the formerly unused tail of the character
record.

## Enhanced equipment

Enhanced characters can earn eight weapon and armor tiers. They are real
inventory items: weight, permanent enchantment, class restrictions, acid
destruction, attack/defense formulas, equipping, status pages, and saves all
use the same systems as original gear.

| Unlock | Weapon | Damage / hit / weight | Armor | Defense / weight |
| --- | --- | --- | --- | --- |
| Floor 375 Abyss King | Worldforged Blade | 75 / +28 / 10 lb | Prismatic Mail | 75 / 36 lb |
| Floor 500 World King | Riftcarver | 130 / +42 / 12 lb | Riftward Plate | 130 / 42 lb |
| Floor 625 Rift Tyrant | Starforged Saber | 210 / +58 / 14 lb | Starforged Mail | 210 / 48 lb |
| Floor 750 Star Eater | Voidreaver | 310 / +74 / 16 lb | Void Bastion | 310 / 54 lb |
| First victory on floor 825+ | Eternity Edge | 430 / +90 / 18 lb | Eternity Plate | 430 / 60 lb |
| Floor 875 Eternity Dragon | Celestial Brand | 570 / +105 / 20 lb | Celestial Aegis | 570 / 66 lb |
| First victory on floor 950+ | Ascendant Edge | 750 / +118 / 22 lb | Ascendant Aegis | 750 / 72 lb |
| Floor 1000 Moraff Ascendant | Moraff's Legacy | 980 / +127 / 24 lb | Moraff's Bulwark | 980 / 80 lb |

The Worldforged Blade is unavailable to Worshippers and Monks. Prismatic Mail
is unavailable to Worshippers and Wizards. Every later tier retains the original
heavy-weapon/heavy-armor professions: Fighter, Priest, and Mage. The floor-950
Final Forge cache is awarded once per save; dropping or losing either item
does not create another cache.

The original eight-line `W` and `A` selectors remain unchanged on their first
page. Enhanced mode adds a full eight-line Page Down page; Page Up
returns to the original equipment. As in WORLD.EXE, an uncollected item's name
is shown only as `--------`, and selecting gear forbidden to the character's
class displays the original multi-line error message.

Beastiary V1-V4 sidecars are migrated to `MWBEST05` on save. Existing kill
counts remain attached to their monster, all bosses appear at their actual
progression positions, and new entries begin undiscovered.

## Super-rare relics

Enhanced monsters can drop five permanent passive relics. Relics are checked
after the original reward chain, never replace an original drop, never
duplicate, and are completely absent/inert in Classic. The base chance begins
near 1 in 4,200 eligible kills and gradually improves toward about 1 in 2,250
at floor 1,000; quest bosses receive a sixfold chance without guaranteeing a
drop.

| Earliest floor | Relic | Permanent effect |
| --- | --- | --- |
| 350 | Ring of Arcane Renewal | Restores 1 spell point every four player actions |
| 475 | Bloodstone Signet | Restores 5% of melee damage as health, capped by player level |
| 600 | Deepward Amulet | Reduces monster damage by 15% and doubles the time between poison/disease stat drains |
| 750 | Sage's Prism | Increases experience from monster kills by 25% |
| 900 | Phoenix Seal | Leaves the player at 1 HP after a lethal monster strike, then recharges for 300 actions |

Pockets → Miscellaneous Magic Items has a second Enhanced-only relic page with
ownership, exact effects, and Phoenix recharge state. View Stats and Game
Stats show collection progress. The Ctrl+F12 trainer exposes the five ownership
fields and Phoenix cooldown only while editing an Enhanced character. The
Enhanced command legend also adds `Spells in Effect 3`, which summarizes every
active automatic magic item and owned relic, including the next Arcane Renewal
spell-point tick and the Phoenix Seal's exact recharge.

## Deep bosses

- Floor 375: **Violet Abyss King**. Its Abyssal Orb sets the equipped
  weapon's enchantment to +200 and unlocks Worldforged gear.
- Floor 500: **Prismatic World King**. Its World Orb sets the equipped
  weapon's enchantment to +300 and unlocks Riftward gear.
- Floor 625: **Cobalt Rift Tyrant**. Its Rift Orb sets the equipped weapon's
  enchantment to +450 and unlocks Starforged gear.
- Floor 750: **Crimson Star Eater**. Its Star Orb sets the equipped weapon's
  enchantment to +600 and unlocks Void gear.
- Floor 875: **Viridian Eternity Dragon**. Its Eternity Orb sets the equipped
  weapon's enchantment to +800 and unlocks Celestial gear.
- Floor 1000: **Radiant Moraff Ascendant**. Its Ascendant Orb sets the equipped
  weapon's enchantment to +1000 and unlocks Moraff's final gear.

All six native bosses are quest-only, tracked independently in the native
quest flags and Beastiary, use the normal in-viewport combat flow, and never
enter the random spawn pool. Floor 1000 is the bottom of the dungeon and
cannot generate a downward ladder.
