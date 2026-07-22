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

The deep monsters use the existing resistance and status systems in new
combinations. Depending on the monster, attacks can breathe fire or cold,
inflict poison or disease, dissolve armor with acid, or drain multiple player
levels. Their attack, defense, save, agility, damage, and HP-factor values
continue increasing across the five generations.

## Widened values

- Dungeon floor IDs and saved monster levels are unsigned 16-bit values.
- Monster definition combat stats are 16-bit values.
- Saved monster HP is an unsigned 32-bit value. Runtime combat remains signed
  32-bit, which is ample for the floor-1000 bosses.
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

Beastiary V1-V4 sidecars are migrated to `MWBEST05` on save. Existing kill
counts remain attached to their monster, all bosses appear at their actual
progression positions, and new entries begin undiscovered.

## Deep bosses

- Floor 375: **Violet Abyss King**. Its Abyssal Orb sets the equipped
  weapon's enchantment to +200.
- Floor 500: **Prismatic World King**. Its World Orb sets the equipped
  weapon's enchantment to +300.
- Floor 625: **Cobalt Rift Tyrant**. Its Rift Orb sets the equipped weapon's
  enchantment to +450.
- Floor 750: **Crimson Star Eater**. Its Star Orb sets the equipped weapon's
  enchantment to +600.
- Floor 875: **Viridian Eternity Dragon**. Its Eternity Orb sets the equipped
  weapon's enchantment to +800.
- Floor 1000: **Radiant Moraff Ascendant**. Its Ascendant Orb sets the equipped
  weapon's enchantment to +1000.

All six native bosses are quest-only, tracked independently in the native
quest flags and Beastiary, use the normal in-viewport combat flow, and never
enter the random spawn pool. Floor 1000 is the bottom of the dungeon and
cannot generate a downward ladder.
