# Enhanced deep-dungeon spells

Enhanced Experience adds 40 spells: ten in each of the original four spell
families. Classic Experience keeps the exact original 30-entry catalog per
family and cannot display, cast, create, or find these additions.

Press Page Down from any spell, scroll, wand, paper, or spell-help selector to
open the deep-spell page. Page Up returns to the original level 1-10 list.
Enhanced selectors show a large directional arrow and the current page in the
top strip; the paging badge can also be clicked.
Deep magic spans levels 11-14 and uses the displayed spell level as its
spell-point cost when cast from a spellbook.

## Unlock progression

One entry in all four families unlocks after the first victory at or beyond
each 100-floor milestone. The unlock mask is stored with the character.

| Floor | Permanent | Preparation | Wizard battle | Priest battle |
|---|---|---|---|---|
| 100 | Enchant Weapon Level 150 | Abyss Descend | Abyssal Lance | Greater Restoration |
| 200 | Enchant Armor Level 100 | Abyss Ascend | Time Stop | Divine Aegis |
| 300 | Body Armor Level 100 | Deep Sanctuary | Void Nova | Holy Cataclysm |
| 400 | Write Deep Scroll | Cartographer's Eye | Soul Rend | Celestial Stasis |
| 500 | Charge Deep Wand | Town Portal | Oblivion | Final Judgment |
| 600 | Enchant Weapon Level 500 | Rift Descend | Starfire | Life Convergence |
| 700 | Enchant Armor Level 350 | Rift Ascend | Chrono Lock | Eternal Ward |
| 800 | Body Armor Level 300 | Eternal Sanctuary | Reality Rupture | Wrath of Heaven |
| 900 | Write Ascendant Scroll | World Reveal | Mana Tempest | Phoenix Prayer |
| 1000 | Charge Ascendant Wand | Soul Anchor | Annihilation | Divine Verdict |

## Permanent spells

Permanent spells retain the original rules: they can be cast only in town,
take one month, and a spellbook cast permanently spends maximum spell points.

| Spell | Effect |
|---|---|
| Enchant Weapon Level 150 / 500 | Raises the equipped physical weapon to at least the named enchantment. |
| Enchant Armor Level 100 / 350 | Raises the equipped armor to at least the named enchantment. |
| Body Armor Level 100 / 300 | Raises innate body armor to at least the named value. |
| Write Deep / Ascendant Scroll | Creates a one-use scroll containing any level 1-14 spell. |
| Charge Deep / Ascendant Wand | Adds ten or twenty charges of any level 1-14 spell, saturating at 255. |

## Preparation spells

| Spell | Effect |
|---|---|
| Abyss Descend / Ascend | Moves 50 floors down/up, relocates safely, and clamps at the dungeon boundary. |
| Rift Descend / Ascend | Moves 100 floors down/up, relocates safely, and clamps at the dungeon boundary. |
| Deep Sanctuary | Protection tier 5 and every resistance for at least 600 turns. |
| Eternal Sanctuary | Protection tier 8 and every resistance for at least 1,200 turns. |
| Cartographer's Eye / World Reveal | Reveals every map cell on the current floor. |
| Town Portal | Returns the caster to a safe position in town. |
| Soul Anchor | Binds the caster's current floor and position as a one-use raise-dead return point. Death consumes the anchor, restores the character through the normal raise-contract flow, and costs one Constitution. It does not restore HP or SP when cast. |

## Wizard battle spells

| Spell | Effect |
|---|---|
| Abyssal Lance | Caster level × 25 + 500 damage. |
| Time Stop / Chrono Lock | Stops a non-immune monster for 30 or 120 turns. |
| Void Nova | 5,000-12,000 damage. |
| Soul Rend | Drains Wisdom × 4 plus half caster level from monster level. |
| Oblivion | 25% maximum monster HP plus 5,000 damage. |
| Starfire | 15,000-30,000 damage. |
| Reality Rupture | 40% maximum monster HP plus 25,000 damage. |
| Mana Tempest | Caster level × 50 plus 2,000 damage. |
| Annihilation | 60,000-120,000 damage. |

## Priest battle spells

| Spell | Effect |
|---|---|
| Greater Restoration | Restores HP and cures poison and disease. |
| Divine Aegis / Eternal Ward | Protection tier 6 for 180 turns or tier 8 for 1,200 turns, plus every resistance. |
| Holy Cataclysm | 3,500-9,000 damage. |
| Celestial Stasis | Stops a non-immune monster for 30 turns. |
| Final Judgment | 20% maximum monster HP plus 4,000 damage. |
| Life Convergence | Deals 10% of the monster's current HP plus 2,000 damage. Half that damage heals the caster, capped at one-third maximum HP; it neither restores SP nor cures status effects. |
| Phoenix Prayer | Heals half maximum HP, cures poison and disease, grants protection tier 7 for 300 turns, and fire resistance for 600 turns. It does not restore SP. |
| Wrath of Heaven | 12,000-26,000 damage. |
| Divine Verdict | 50% maximum monster HP plus 30,000 damage. |

## Scrolls, wands, and papers

- Scrolls and papers are consumed after one successful cast.
- Wands consume one charge per successful cast.
- Item casts require no spell points.
- A failed or invalid cast does not consume the item.
- Fighters retain the original restriction: they can cast from magic paper,
  but not from scrolls or wands.

Starting at floor 100, normal spell-item treasure can choose from the deep
catalog. One more deep entry enters its drop range every 100 floors. Deep and
original magic then have equal selection weight.

The original character record already reserved 45 slots for every spell and
item family. Enhanced uses slots 30-39. Native save version 6 tracks the ten
legitimate unlocks and migrates older five-spell Enhanced saves automatically.
