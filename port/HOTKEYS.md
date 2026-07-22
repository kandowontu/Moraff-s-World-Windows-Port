# Moraff's World native-port hotkeys

These additions are testing and convenience controls in the Windows port.
They do not replace any keyboard command from the original DOS game.

| Shortcut | Function |
| --- | --- |
| `Ctrl+F5` | Open the graphics/model viewer for every `WORLD.PIC` record, `WALL.PIC` record, and loaded 1024×768 font glyph. |
| `Ctrl+F6` | Ask whether to reroll the dungeon. Yes creates a new dungeon seed, clears that dungeon's monsters and pit history, and places the character in its floor-zero town. |
| `Ctrl+F7` | Toggle Open Floor Mode. While enabled, `U` and `D` can move up or down one floor when no matching ladder is present. |
| `Ctrl+F8` | Teleport directly to floor-zero town. |
| `Ctrl+F9` | Toggle God Mode. Incoming hit-point damage is blocked and learned spells require and consume no current or maximum spell points. |
| `Ctrl+F10` | Toggle noclip. Movement can pass through dungeon walls and rock, but cannot leave the map boundary. |
| `Ctrl+F11` | Enter or leave the non-persistent wilderness test sandbox. |
| `Ctrl+F12` | Open or close the character trainer. While the Beastiary is open, unlock every entry for the current session instead. |
| `G` | Open Game Stats for the current save. This is also clickable in the command legend. |

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
trainer unlock are runtime-only. They reset when the program closes. A dungeon
reroll changes the active game world and is retained the next time the game is
saved.
