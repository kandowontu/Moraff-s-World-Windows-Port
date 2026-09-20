# CHCHAR.EXE static reconstruction

This is evidence-backed pseudocode for the character creator in Moraff's
Revenge Advanced 3.3. Addresses refer to the statically decoded QuickBASIC 3
module listing. No DOS program was executed to produce it.

## Control flow

`character_creation_main` (`0040`) allocates the original numeric and string
arrays, loads the four race names and the two compiled `DATA` statements,
seeds QuickBASIC's random-number generator from `TIMER` (`0564`-`056A`), shows
the characteristic/class explanation pages, then performs this flow:

```text
choose one of Human, Dwarf, Elf, Hobbit
repeat
    roll the six characteristics and starting health
    ask "Do you want it (Y, N, OR ESC)?"
until accepted or Escape chains back to BEGIN
choose class 1 (Fighter) or 2 (Wizard)
read, uppercase, and validate a character name
replace the existing name only after Y/N confirmation
write the exact 311-record character stream
create the paired BSAVE sidecar and update F5.COM
chain back to BEGIN
```

The race chooser wraps at both ends (`0965`-`0A8C`). Return accepts the
highlighted race. It recognizes the two-byte Right (`00 4D`) and Left
(`00 4B`) strings only; the displayed race columns are 7, 14, 21, and 26 on
the 40-column grid selected by `WIDTH 40` at `091E`-`0921`. The roll prompt accepts `Y`, rerolls on `N`, chains back
to `BEGIN` on Escape, and leaves the same roll displayed after every other
key (`0E33`-`0E61`). Class input is rejected until it converts to a value in
the inclusive range 1 through 2 (`0E7B`-`0EAD`); Escape has no special meaning
at that prompt.

Names are normalized one character at a time (`0F36`-`0FA5`): every byte
greater than ASCII 92 has 32 subtracted. This uppercases `a`-`z`, but also
maps `{|}~` to `[\\]^`, exactly as the compiled code does. An empty name
restarts name input. `character_roster_find_name` (`1560`) detects
duplicates; replacement requires confirmation (`1088`-`10C2`).

The native controller now follows those input boundaries. It restores both
complete 80-column explanation pages (`0736`-`0919`), then preserves the
original 40-column mode for the race chooser, roll, class, knife, and name
screens. It uses the recovered `Laziness` label instead of substituting
`Luck`, seeds the recovered QB3 RNG through `RANDOMIZE TIMER` rather than
process uptime, overwrites the existing numbered pair only after
duplicate-name `Y`, and returns directly to BEGIN after writing the save. The
earlier native-only `CHARACTER CREATED` modal has been removed.

The supposed disk-free-space test is actually a fixed ten-character roster
limit. `character_roster_find_name` returns the number of names before the
`END` record in the numeric variable at `1B02`. The initialized MBF singles at
`1B56` and `1AFE` are 9 and 10. At startup (`037A`-`038B`) a count of 10 prints
`There is no more room on this disk.` and returns to BEGIN. After name entry
(`0FBD`-`1000`), a count of 9 prints `This is the last character that will fit
   on this disk.` and waits; a count of 10 refuses the write. The native roster
and controller now use that exact limit, ordering, text, and key wait.

`character_advance_text_color_cycle` (`16E8`) is also fully recovered. It
increments a persistent display counter, wraps values above 6 to zero, uses
that counter to index the seven-entry color table at DS:`18AE`, and passes the
selected value to QuickBASIC `COLOR`. It is a deterministic page/row color
cycle and does not call the random-number generator. Startup builds that table
as `9,10,11,12,13,14,15` when color is enabled and seven copies of `15` when
it is disabled (`03F0`-`0480`). Since the counter starts at zero and is
incremented before lookup, the first explanation section uses attribute 10.
Both bottom prompts explicitly load table entry 6, attribute 15.

`character_print_hit_any_key_and_wait` (`1676`) performs `LOCATE 25,15`,
prints `HIT ANY KEY`, and falls directly through into the nonempty-key loop at
`168E`. The older `character_print_blank_line` label was incorrect and has
been removed from the source map.

After the explanation, the chooser uses normal foreground 9 in color mode or
7 in monochrome. The selected race is black text on that normal-color
background; the other three are normal-color text on black (`094B`-`0A10`).
The accepted race heading remains on screen while the roll is printed from
BASIC row 3. Its six `PRINT USING` rows are immediately consecutive, followed
by a blank row, `TOTAL`, `Health points`, another blank row, and the Y/N/Escape
prompt. Class and knife messages append beneath the accepted roll rather than
opening a replacement screen (`0BD9`-`0ED0`).

Color mode also performs one cosmetic `RND` call after distributing the
attribute points and before deriving money/health. The result selects an
attribute from 9 through 15 for the entire roll. This branch is controlled by
the `NAME` color flag at DS:`1B46`, not by the Human race; therefore it occurs
for every race and changes the subsequent money/health RNG stream. Monochrome
does not consume that call and keeps attribute 7.

## Exact race and roll algorithm

The first compiled numeric `DATA` statement is decoded by
`extract_qb3_read_data.py`. Rows are Strength, Intelligence, Wisdom, Health,
Agility, Laziness:

| Race | Base row |
| --- | --- |
| Human | `4, 4, 4, 4, 4, 4` |
| Dwarf | `4, 1, 1, 7, 7, 4` |
| Elf | `2, 6, 5, 3, 5, 3` |
| Hobbit | `2, 2, 2, 5, 9, 4` |

Every row totals 24. A roll copies the chosen row, chooses
`INT(RND(1) * 10) + 52` extra points, and repeats that many times:

```basic
attribute(INT(RND(1) * 6) + 1) =
    attribute(INT(RND(1) * 6) + 1) + 1
```

The generated total is therefore 76 through 85. The implementation evaluates
the random index once per point; the repeated expression above is descriptive,
not two separate random calls.

## Derived values

These formulas are direct translations of `0CCE`-`0E01`. `INT` floors toward
negative infinity; `FIX` truncates toward zero, matching QuickBASIC.

```text
health_growth = INT(health * 3 - 39)
if health_growth < 1:
    health_growth = FIX(health_growth / 3)

attack = FIX(strength - 11)
if attack < 1:
    attack = FIX(attack * 0.5)

agility_defense = agility - 12
if agility_defense < 1:
    agility_defense = 0

pocket_money = INT(RND(1) * 10) + 11
health_random_ceiling = INT(RND(1) * 15) + 2
health_random_part = INT(RND(1) * health_random_ceiling)
health_max = INT(RND(1) * 10) + health_growth + health_random_part
health_current = health_max
```

The spell-capacity path begins with (`10E2`-`1114`):

```text
spell_capacity_intermediate =
    INT(intelligence * 0.5 + wisdom * 0.4 - 10.8)
```

At the initial level zero, the remaining class formula reduces exactly to:

```text
Fighter: spell_points = MAX(0, spell_capacity_intermediate - 4)
Wizard:  spell_points = MAX(0, spell_capacity_intermediate + 2)
```

DS:`19B0` is not dedicated spell-capacity storage. Whole-module reference
analysis proves that the compiler reuses the same MBF single as a generic
numeric scratch during attribute rolls, name uppercasing, this calculation,
and obfuscated save serialization. Its recovered source-map name is therefore
`shared_single_scratch`; the name above describes only this expression.

The rest of the creator's formula is now resolved from the matched QB3
runtime operators.  Selector `3F:8F` is the MBF-single multiply path,
`3F:89` is divide, and `3F:7F` is add.  The assignments at `1142` and `1166`
write the scaled aptitude back before the fixed class term is added at
`119E`:

```text
Fighter:
    scaled = FIX(spell_capacity_intermediate * player_level / 6)
    spell_points = spell_capacity_intermediate + scaled + player_level - 4

Wizard:
    scaled = FIX(spell_capacity_intermediate * player_level / 3)
    spell_points =
        spell_capacity_intermediate + scaled + 3 * player_level + 2

spell_points = MAX(0, spell_points)
```

The creator initializes `player_level` to zero, so these reduce to the two
initial formulas above.  This creator-only aptitude formula is intentionally
different from the one recomputed by the inn at `DUNSMALL:2115`; the port
preserves that original divergence.

## Initial persistent state

The writer at `1244`-`1557` proves the following gameplay values before their
storage offsets are applied:

```text
experience = 0
player_level = 0
equipped_armor = 0
status_flags = 0
carried_weight = 150
carried_treasure = 0
bank_money = 0
pending_experience = 0
player_x = 10
player_y = 10
dungeon_level = 0
dungeon_generation_seed = 1
```

Persistent value 1 owns the starting knife. Persistent value 21 stores the
one-based race number; this is independently confirmed across all five
original save slots. A Wizard also receives the initial spellbook mask pair
written at `11E1`-`11FA`.

The sequential writer applies the same obfuscating transforms later removed by
`DUNSMALL:B674`: attributes are `gameplay * 3 + 237`; experience, level,
maximum health, current health, weight, treasure, and pocket money receive
their schema offsets. See `revenge_save_schema.json` for the complete typed
311-record order.

## QuickBASIC random generator

`BRUN30:B2C7` proves a 24-bit linear congruential generator. Its update is:

```text
state = (state * 0xFD43FD + 0xC39EC3) AND 0xFFFFFF
RND = state / 16777216
```

`RANDOMIZE TIMER` calls `$TMR` followed by `$RZ1`; `$RZ1` XORs the two words of
the MBF single and places the result into the upper two bytes of the 24-bit
state (`BRUN30:B349`-`B353`). This is the generator the native implementation
must use, not the C library `rand()`.
