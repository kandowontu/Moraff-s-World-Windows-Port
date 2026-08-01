# Classic and Enhanced Experiences

The experience is selected when creating a character and is saved with that
character. Existing saves created before this option was added remain in
Enhanced mode.

| Feature | Classic Experience | Enhanced Experience |
|---|---|---|
| Total floors | 251: town floor 0 plus dungeon floors 1-250 | 1001: town floor 0 plus dungeon floors 1-1000 |
| Races | Original eight races | Original races plus Dragonkin and Celestial; Dragonkin favor STR/CON, while Celestials favor INT/WIS/LUCK |
| Classes | Original seven classes | Original classes plus Spellblade and Paladin; both use late martial gear, with Spellblades learning wizard magic and Paladins learning priest magic |
| Monster progression | Original monster roster through floor 250 | Original roster plus new variants and late-game monster tiers on floors 251-1000 |
| Quest progression | Original eight quest bosses through floor 200 | Original quest chain plus bosses at floors 375, 500, 625, 750, 875, and 1000 |
| Quest rewards | Original equipment rewards through the final classic boss | Additional +200, +300, +450, +600, +800, and +1000 weapon-enchantment orbs plus eight late weapon/armor tiers distributed from floor 375 through 1000 |
| Spell catalog | Original 30 spells in each of four families | Original catalog plus 60 deep spells (15 per family), progressively unlocked through floor 1000 with matching scrolls, wands, and papers |
| Super-rare relics | None | Five passive relics begin dropping from floor 350: spell regeneration, melee life-steal, deep damage/status protection, bonus experience, and lethal-strike survival |
| Effect display | Original Spells in Effect pages 1 and 2 | Original pages plus clickable page 3 for automatic magic items, relic mechanics, SP-regeneration timing, and Phoenix recharge |
| Digging | Original depth rules; allowed through floor 120 | Proportionally extended depth rules; allowed through floor 480 |
| Ascend magic | Original cutoff at floor 65 | Proportionally extended cutoff at floor 260 |
| Descend magic | Original cutoff at floor 123 | Proportionally extended cutoff at floor 492 |
| Major Descend landing cap | Floor 75 | Floor 300 |

## Traversal enforcement

The selected mode is applied to every mechanic that can change dungeon depth,
not only ordinary ladders. In Classic mode:

- Downward ladders and Open Floor Mode stop at floor 250.
- Hidden pitfalls cannot fall beyond floor 250; keyed trapdoors retain their
  original floor 10-170 destinations.
- Digging uses the original floor 120 cutoff, floor 124 search reversal,
  floor 150 direction rule, floor 16 slow-dig threshold, and 130-attempt
  search budget.
- Descend stops working after floor 123. Ascend, Double Ascend, Major Ascend,
  and Major Descend stop working after floor 65. Major Descend cannot land
  below floor 75.
- Floor Sloshers, trainer floor edits, save loading, and raise-contract returns
  are clamped to the Classic floor range. Teleport stones still return to
  town floor 0, and Relocate remains confined to the current floor.

Enhanced mode uses the corresponding scaled traversal values shown above and
the explicit deep-dungeon geometry documented in `DEEP_DUNGEON.md`. Its
additional magic is cataloged in `DEEP_SPELLS.md`.

Both modes retain the native port's interface, mouse controls, Beastiary,
shops, trainer, model viewer, bug fixes, save integrity checks, and optional
testing hotkeys. The choice controls dungeon scale and progression rather than
removing quality-of-life features.

The selected experience and its maximum floor are shown on the Game Stats
screen (`G`).

The character/trainer level cap is 3,000 in both modes. Dungeon depth remains
mode-specific and is independent of player level.
