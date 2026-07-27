# Moraff's World native-port hotkeys

These additions are testing and convenience controls in the Windows port.
They do not replace any keyboard command from the original DOS game.

| Shortcut | Function |
| --- | --- |
| `Ctrl+F5` | Open the graphics/model viewer for every `WORLD.PIC` record, `WALL.PIC` record, and loaded 1024×768 font glyph. |
| `Ctrl+F6` | Ask whether to reroll the dungeon. Yes creates a new dungeon seed, clears that dungeon's monsters and pit history, and places the character in its floor-zero town. |
| `Ctrl+F7` | Toggle Open Floor Mode. While enabled, `U` and `D` can move up or down one floor when no matching ladder is present. |
| `Ctrl+F8` | Ask for Y/N confirmation, then teleport directly to floor-zero town if confirmed. No or Escape leaves the current location unchanged. |
| `Ctrl+F9` | Toggle God Mode. Incoming hit-point damage is blocked and learned spells require and consume no current or maximum spell points. |
| `Ctrl+F10` | Toggle noclip. Movement can pass through dungeon walls and rock, but cannot leave the map boundary. |
| `Ctrl+F11` | Enter or leave the non-persistent wilderness test sandbox. |
| `Ctrl+F12` | Open or close the character trainer. While the Beastiary is open, unlock every entry for the current session instead. |
| `Ctrl+Shift+Alt+F12` | Ask for Y/N confirmation, then maximize the current character using mode-safe caps. This fills every valid inventory and magic catalog, learns all mode-valid spells, equips and enchants the strongest available gear, completes the applicable quest chain, clears harmful conditions, grants all-class equipment/magic access, and maximizes HP, SP, level, attributes, banked jewels, stones, keys, and beneficial effects. Jewels already carried in the pocket are deliberately left unchanged, and the permanent maxed Feather effect reduces loaded weight to zero despite the filled inventories. The changes become persistent with the next normal save. |
| `G` | Open Game Stats for the current save. This is also clickable in the command legend. |
| `3` | Enhanced only: open Spells in Effect page 3 for automatic magic items, passive relics, Arcane Renewal timing, and Phoenix recharge. This is also clickable in the command legend. |

## Character trainer controls

The trainer follows the selected experience mode. Classic shows the original
eight weapons, eight armors, and 30 spells per family. Enhanced adds a complete
second page of eight weapons and eight armors plus ten deep entries in every
spell, scroll, wand, and paper family. The page heading shows `CLASSIC` or
`ENHANCED`; hidden Enhanced entries cannot be selected or edited while a
Classic character is active.

| Page | Controls |
| --- | --- |
| Character / Stats | `Up`/`Down` selects; `Left`/`Right` changes by 1; `Page Up`/`Page Down` changes by 100; typing a number and pressing `Enter` sets it. |
| Spells | `Space` toggles the selected learned spell; `A` learns the visible family; `N` clears it. |
| Scrolls, Wands, Papers | `Space` or `+` adds one; `-` removes one; `Page Up`/`Page Down` changes by 10; `M` maximizes the selected item; `A` maximizes the visible family; `N` clears it. A typed number followed by `Enter` sets the selected count. |
| Equipment | `Space` or `+` adds one copy; `-` removes one; `M` sets 255 copies; typing a number and pressing `Enter` sets the count. `Left`/`Right` changes enchantment by 1 and `Page Up`/`Page Down` changes it by 100. |
| Effects | Character-page numeric controls apply. |

Inventory counts and magic-item charges are unsigned bytes (`0`-`255`).
Classic applies the original trainer's byte/signed-byte and 32,767 word caps.
Enhanced enables the widened signed/nonnegative 16-bit equipment and permanent
bonus ranges, 65,535-turn counters, and protection tier 8. Coordinates stop at
the map boundary, while floor depth stops at 250 in Classic or 1000 in
Enhanced. Player level stops at 1000, core attributes at 32,767, and spell
points at the largest exactly representable single-precision value below
2^32. Enhanced HP uses the full unsigned 32-bit range; Classic retains its
32,767 cap. AGE is displayed and edited as the character's age in years; the raw
save value is an internal time counter, not the number that the trainer now
presents.

## Graphics/model viewer controls

| Control | Function |
| --- | --- |
| `Page Up` / `Page Down`, or `A` / `D` | Select the previous or next graphic, wrapping at both ends. |
| `Tab` | Cycle through `WORLD.PIC`, `WALL.PIC`, and the loaded font glyphs. |
| `Home` / `End` | Select the first or last graphic in the current set. |
| Mouse wheel, `Up` / `Down`, or `+` / `-` | Adjust fit-relative zoom in smooth 0.05× steps, from 0.05× through 20×. |
| `Left` / `Right` | Rotate by one degree. |
| `[` / `]` | Rotate by five degrees. |
| `,` / `.` | Fine rotation in 0.1-degree steps. |
| `R` | Restore fit zoom and zero rotation. |
| `Escape` or `Ctrl+F5` | Return to the game. |

Noclip, God Mode, Open Floor Mode, the wilderness sandbox, and the Beastiary
trainer unlock are runtime-only. They reset when the program closes. Dungeon
rerolls and confirmed max-character changes alter character/world data and are
retained the next time the game is saved.
