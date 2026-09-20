# DUNSMALL.EXE static reconstruction

This is an address-backed high-level reconstruction of the original
QuickBASIC 3 dungeon module. It is derived from the executable and BRUN30
runtime tables without running the DOS game. Addresses are offsets in the
module's recovered code or data segment.

## Module handoff and save load

`BEGIN.EXE` writes three sequential records before chaining to DUNSMALL:
the color flag, selected character name, and selected numeric slot. DUNSMALL
reads them at `00E4-00FF` into DS:`B462`, `B466`, and `B46A` respectively.

The character save is a 311-record sequential file. `B674-B93B` reads the
scalar records and the four persisted array groups; `B308-B57D` writes them.
All QuickBASIC arrays have a physical element zero, while the persistence loops
use BASIC indices 1 through N. Consequently the first saved value is four
bytes after each mapped array base.

The twelve paired spellbook records are written battle first (DS:`5EE0`) and
preparation second (DS:`5EAC`) at `B516-B53C`. The menu validator at
`C5D0-C609` selects preparation when the casting type is 1 and battle when it
is 2.

There are exactly five direct calls to the character writer: main-screen `Q`
at `0D91`, the pre-fall chute boundary at `348B`, the fountain restart at
`3F4E`, pause-menu `Q` at `802A`, and the class-five level-drain boundary at
`9E92`. These are synchronous disk writes. In particular, the chute saves
before incrementing the floor, and the level-drain path saves after changing
experience/level/maximum health but before its following deep-monster Strength
decrement. The native session therefore flushes these recovered snapshots at
the corresponding command boundary; it does not defer them until the player
eventually returns to the roster.

## Startup review and sound (`0514-05EE`, `BA10-BB9E`)

Every DUNSMALL invocation asks `Sound (Y or N)?`. DS:`B4BC` begins at one
(disabled); only `Y` changes it to zero. Before printing that prompt, `0514`
calls `B9B3`, consuming `RND` to select one of the first six entries in the
startup color table. After loading the selected character, `BA10-BB9E`
changes to SCREEN 0 / 80 columns. `BA15` consumes a second `RND` for the
review's default text color, and `INT(RND*6)+1` at `BA1E` consumes a third to
select `REVIEW.1` through `REVIEW.6`. Up to 24 sequential records are printed.
A leading digit 1 through 7 is removed and selects the corresponding startup
color-table entry; a line without a digit keeps the random default. The file's
`|` marker is replaced with a comma. `BB9E-BBC8` ends the SCREEN 0 page with
`Please read this...` and starts the separate loading score. After the
character load returns, `057A-0591` switches to SCREEN 1, prints
`HIT ANY KEY`, and waits for one nonempty key. The 18-poll keyboard drain at
`059A` follows that acknowledgement before the first floor initialization.
The shared nonempty-key reader at `2F71` has a non-obvious state side effect:
after acquiring the raw key, it unconditionally calls `2F43` and clamps every
one of the six character attributes to a minimum of one. It does not fold
case. This side effect also applies to its town, pause, guild, spell-result,
loot-book, and active-combat `7DC9` call paths; it is not merely an input API.

`05A0-05EE` is a three-score QuickBASIC `PLAY` dispatcher, not a generic beep
toggle. The exact initialized strings are:

- temple, DS:`BA5E`: `T135O3L8CEFGL4<<C<G>>>L8CEFGL4<CL8<G>C>CEFL4GECEDL8G.L16AL8GFEDL4C.L8CL4EL8GGGF4L8<<G+L4F>>L8EFL4GECDL2C`
- death, DS:`BAB8`: `T90O1MNL4F.FL8FL4F.G+L8GL4GL8FL4FL8EL4F.`
- sleep, DS:`BA7C`: `T250O3MNL4E.G8>ED2C<E.G8>C<B2.F.>C8FE2CDC<AG2.E.G8>ED2C<E.G8>C<B2.F.>C8FE2CD<AB>C2.`

The dispatcher prepends BASIC's `MBX` background/expression mode. With sound
disabled, the sleep dispatcher calls the four-second TIMER wait instead.
King's Inn performs one additional unconditional four-second wait. Temple and
death call the first and second score selectors respectively. `O` toggles
DS:`B4BC`, prints `SOUND ON` or `SOUND OFF` for two TIMER seconds, and then
overprints nine spaces (`1055-10BA`).

## Exact initialized-data image

DUNSMALL's entry stub calls a one-instruction module initializer. The bytes
beginning at the paragraph after that initializer's `RETF` are copied as one
contiguous image into DGROUP; the source and destination bounds are encoded in
the module itself. The exact mapping is:

- executable file offsets `C9F0` through `EB72`
- DS offsets `B7D0` through `D952`
- constant source-to-destination delta `1220h`
- 8,578 bytes
- SHA-256 `FB778E88C02FAC1BA44615BEEB26182B2D23EC482B1CB5A93753F77244E5CD99`

This image contains 386 recovered string descriptors/payloads. Referenced
non-string constants in it are decoded as MBF singles or doubles at their
exact DS addresses in the generated listing. An eight-byte MBF double often
begins with four zero mantissa bytes; the disassembler retains the eight-byte
view instead of incorrectly annotating those operands as single-precision
zero. No executable code is being guessed to be data.

## Town economy and services (`1E0A-2EF2`)

Town is not a separate menu. Routine `10FD-12C5` compares the floor-zero
player coordinate and stores a one-based service ID. If nonzero, `12EE-1327`
prints `There's a rope above. Hit U to climb it.`; pressing `U` reaches the
seven-entry ON-GOTO table at `132A`:

| ID | Coordinate(s) | Destination |
| ---: | --- | --- |
| 1 | `(7,3)` | Flea Bag Inn `1E0A` |
| 2 | `(3,2)` | Yuppydom Inn `1F3D` |
| 3 | `(18,17)` | Kings Inn `1FCD` |
| 4 | `(13,3)` | Bank `22F7` |
| 5 | `(7,15)`, `(14,12)` | Temple `2522` |
| 6 | `(18,3)`, `(13,18)`, `(2,8)` | Store `281E` |
| 7 | `(6,14)` | Wizard's Guild `2BB8` |

The inn prices are embedded MBF doubles: Flea Bag 10 JP, Yuppydom 200 JP,
and Kings 6,000 JP. Flea Bag restores one HP, has a 10% robbery roll and a
separate 10% sickness roll. Sickness removes one Health characteristic,
clamps all six characteristics to a minimum of one through `2F43`, holds its
three-line warning for eight seconds, and sets disease to one. Yuppydom
restores three HP and has a 5% robbery roll. DS:`B558` bit zero makes either
inexpensive inn heal fully. Kings heals fully and has neither adverse roll.
Every successful stay then settles all pending experience. A robbery clears
pocket money, the three ordinary weapon ownership fields, and magic sword and
mace bonuses; it does not clear armor or the bank balance.

Entering the bank automatically converts all carried treasure into pocket JP,
zeros the carried-treasure field, and stores `.5` in the bag coin-content
slot. It then recomputes carried weight as 150 plus 25 per ordinary armor code;
DS:`B558` bit 5 (magic armor) removes that armor weight. Bank input accepts
only digits and transfers the requested amount or the entire available source
balance, whichever is smaller.

The temple's exact price/effect table is:

| Choice | Service | JP | Effect |
| ---: | --- | ---: | --- |
| 1 | Cure wounds | 75 | `health += INT(RND*8)+4`, clamped |
| 2 | Heal all wounds | 1,000 | health becomes maximum |
| 3 | Cure disease | 400 | disease becomes zero |
| 4 | Remove poison | 20,000 | poison becomes zero |
| 5 | Gain level | 500,000 | one direct level/HP award |

The temple deducts the fee before testing whether disease or poison is
present, so those two services still charge when they produce the original
`You don't feel any different.` result.

The store sells Knife 10, Mace 200, Sword 200, Leather 200, Chain 500, Plate
3,000, and Field Plate 10,000 JP. Class 2 (Wizard) may buy the Knife but is
rejected from choices 2 through 7. Weapon purchases set ownership; armor purchases directly
replace the equipped armor code and reject a non-upgrade. The optional Town
line appears when the character does not own it and pocket plus bank exceeds
99,999 JP, but the actual purchase requires 1,000,000 JP in the pocket. The
wizard's guild charges 800 JP for magic-item information. Spell information
instead costs `INT(220 * spell_level^1.75)` for a selected level 1 through 6
(`2DAC-2DC3`). Both paths check funds before the nested P/B picker and deduct
only after it returns a nonzero selection. `revenge_town_schema.json` records
the complete address-backed table, and `port/src/mr_town.c` plus the native
session controller are its state and input transcriptions.

## Input, movement modes, and timing (`087F-10FC`, `2F1A-2FF6`)

The dungeon does not maintain a custom held-key state or repeat timer. Its main
loop polls QuickBASIC `INKEY$` once at `087F`; an empty result returns through
`0921-092C`, while a held key repeats only when DOS BIOS typematic handling
places another key event in the buffer. A native port must therefore emulate
the original typematic cadence at its input boundary instead of advancing on
every rendered frame.

There is nevertheless a separate held-arrow acceleration interaction in
`draw_status_and_map` (`4100-4260`). The configured `E`-command busy-loop
delay runs only when the current cell's explored bit is nonzero and DS:`B5FA`
is at most three. Its exact loop bound is
`INT(configured_delay/6)*B5FA`, so counter zero has no iterations and the
1/2/3 states progressively multiply the work before values above three bypass
it. Later in the same pass, `4208` performs a distinct nonblocking `INKEY$`
lookahead. A queued arrow string is consumed there and increments B5FA; an
empty or non-arrow sample resets it. The main dispatcher at `087F` therefore
receives the next DOS typematic event, not the event consumed by this
lookahead. This is not a second repeat generator—the BIOS still supplies every
repeated key event.

The four arrow strings are the ordinary QuickBASIC extended pairs `00 48`,
`00 4D`, `00 50`, and `00 4B`. Escape toggles DS:`B524` between two control
modes. Mode zero is the default first-person scheme: Up moves forward,
Right turns clockwise, Down turns 180 degrees, and Left turns counterclockwise.
Mode one makes the same arrows move north/east/south/west absolutely while
also setting the facing direction. Direction values are 1=N, 2=E, 3=S, 4=W;
the initial facing is west.

BRUN selector `3F:BB` has Microsoft's public symbol `$LENB`, but handler
`9BC2-9BD5` dereferences the string descriptor and returns its first data byte.
The game uses it as `ASC`, comparing the result with Return 13, Escape 27, and
ASCII case bounds. Routine `2F87` uppercases values strictly greater than 96
and strictly less than 122. The original therefore converts `a` through `y`
but, unusually, leaves lowercase `z` untouched.

Each successful one-cell step increments DS:`B474`; turning in place does not.
All four movement routines look up a destination monster before computing the
procedural wall, but the occupancy result blocks the step only while the
combat view-depth flag is one.  During exploration a passable destination may
be entered even when occupied; the later combat-entry boundary takes
ownership.  During combat the occupied destination remains blocked.

The successful-step tail at `0A4F-0AB0` also wraps DS:`B474` from values over
16 back to one, then compares it with the two battle-spell markers. Matching
Battle Speed clears its marker and removes 11 Agility; matching Battle
Strength clears its marker and removes seven from the attack factor. Neither
effect is cleared merely because combat ends. The spell stores
`(movement_turn == 1 ? 16 : movement_turn)-1`, so the duration is tied to this
cyclic movement counter rather than elapsed time or combat rounds.

The apparently named “wait for key release” helper is instead exactly 18
consecutive `INKEY$` polls (`2FCB-2FF6`). The TIMER helper at `2F1A` waits two
seconds, and `2F35` calls it twice for a four-second message. Separately, the
Options delay prompt stores a digit-only value in DS:`B542`, clamps values over
3000 to 3000, and consumes it in the monster/view update busy loop. These
facts are recorded in `revenge_input_schema.json`; `port/src/mr_input.c`
contains the native command dispatcher and regression tests.

The dungeon polling path is not idle. Startup routine `BF60-BFB9` first waits for a
fresh TIMER tick, counts a tight loop for one second in DS:`B6D0`, and divides
that count by the embedded single-precision constant 326. Routine
`7EEC-7F42` then computes

```text
interval = MAX(INT((165 - monster_level + player_level)
                   * speed_calibration / 20), 8)
```

on every dungeon main-loop pass. It consumes one `RND`, and when
`INT(RND*interval)=1` it calls `7001`. Static control flow proves that `7001`
is not a drawing routine: it advances the floor-global cursor at DS:`B4FE`,
wraps it against `40*dungeon_level`, copies the selected record to DS:`B530`,
and enters `70A1` only for an odd global record. Thus the original deliberately
allows floor monsters to advance while `INKEY$` is empty; the one-second
calibration attempts to make the polling probability independent of CPU
speed. This is separate from DOS keyboard typematic repeat and from the
user-configured 0..3000 busy-loop delay.

The keyed-pass order is asymmetric and observable in the random stream.
Exploration samples `INKEY$` at `087F` and then, on nonzero floors, reaches
`7EEC` at `08F3` before testing whether that sampled string was empty.
Combat reaches `7EEC` at `86E2` before sampling `INKEY$` at `86E5`. Thus a
key which is already buffered still owns one monster/RND update; returning the
key first is not equivalent. Town skips `7EEC` through the level-zero branch.
The native session implements this independently of its blocking DOS-key
adapter. `mr_read_dos_key_with_idle_monsters` schedules 326 normalized polls
per second, performs the recovered first-pass update immediately, preserves
the distinct exploration/combat sample order, and passes calibration `1` to
`mr_session_idle_monster_poll`.
Algebraically this is the same normalization as BF60's measured
`loops_per_second/326`, while avoiding any dependence on modern CPU speed.
The headless session test fixes the QuickBASIC seed so the one-RND poll,
increment-before-parity order, even-record return, and odd-record pursuit
entry are deterministic.

This work is not limited to the exploration loop. Combat polls through
`86E2-86FB`, which also calls `7EEC` once per empty INKEY$ pass. The shared
reader `7DC9-7E8C` first samples INKEY$, then calls `7EEC` even when a key was
already waiting, and is called by spell levels and spell choices, wands,
pills, battle items, drop treasure, the defeat Return loop, and both coin-loot
reads. Consequently those menus do not freeze unrelated floor monsters or the
RNG stream. Level-zero callers still consume the gate RND because `7001`, not
`7EEC`, owns the later town early return. The native controller now has a
separate modal implementation for this exact boundary. If the player's cell
is occupied and combat initialization DS:`B60E` equals one, `7E41` tail-calls
the raw nonempty reader at `2F71`: the key sampled at `7DCF` is discarded and
the game blocks for a fresh key before applying the six-attribute clamp. A
nonblocking second read is not equivalent and can return an empty command.

The combat loop is not a separate movement-locked mode. At `871F` it calls the
ordinary arrow routine before comparing the active monster coordinates with
the player's at `8752-877E`. Separation enters `8E76-8F67`; it is not an
unconditional escape. The level-70 fountain exits immediately. Otherwise a
behavior-three monster with a positive carried damage roll pursues directly,
while other monsters test `INT(RND*3)`, then
`INT(RND*RND*100)+speed_bonus-11` against equipped armor times Agility, followed
by an independently consumed invisibility `INT(RND*5)` gate. A pursuer moves
through `70A1`, then `INT(RND*2)` and `INT(RND*50)-5` decide whether it also
strikes before combat resumes.
When the coordinates still match, `8783` compares literal one with DS:`B51C`.
The loop reset at `8707` stored zero and no intervening path writes one, so
`JNE 8791` enters the player's H/S/M/K/P/F/C/I/T/W/B command dispatcher.
Treating ordinary overlap as the equality branch at `878E` instead discards
the player's command and jumps directly to the monster turn at `9A2F`; that
was the cause of the native port's apparent inability to attack an enemy in
the combat square.
Behavior class three has an unusual attached-strike shortcut at `9A58-9A84`:
when its preceding damage value is positive, the monster retains its prior
attack score, replaces the damage accumulator with exactly 21, and skips the
ordinary d20/defense setup. This is a fixed damage seed rather than a reset.
The player-attack prose has two distinct one-based F2.COM ranges. A zero-
damage ordinary weapon attack selects 1 through 5 at `8D38-8D83`; every
positive attack, including BREATHE FIRE, selects 6 through 10 at `8D86-8DBB`.
Exactly one of those branches consumes `RND` before defeat/initiative logic.
Before either branch, `8D00-8D35` blanks 25 columns on BASIC rows 11 and 10.
The selected prose lands on row 11; positive damage is reported on row 10.
Neither message replaces the row-one attacking-monster announcement.
Monster output is similarly positional: CAN'T STRIKE and normal damage use
BASIC row 7, STUCK uses row 5, SQUASH occupies row 7 and pushes `IT DID...`
to row 8, and status/drain prose begins at row 20. A behavior-five strike
selects and displays F2.COM combat message 11 through 15 before `LEVEL
DRAINED!`; that `RND` is not merely cosmetic state to discard.
The player result is then held for one `TIMER` second at `8E12-8E34`. Normal
monster output receives the final one-second hold at `9FEC-A00E`. SQUASH,
health drain, strength drain, and disease/agility drain set DS:`B730`; that
flag adds a two-second wait and the fixed 18-poll drain at `9F8E-9F94` before
the optional repeat-strike test.

Defeat input has a stricter boundary than a generic any-key screen. The game
prints `YOU KILLED IT!!`, prints `HIT RETURN`, drains 18 `INKEY$` polls at
`A511`, and loops until the first byte is ASCII 13 at `A514-A520`. The coin
take/leave choice drains again at `A997`. The generic-drop path drains at
`AC8D`, prints `YOU FIND...`, and waits four seconds at `ACA2` before revealing
the selected item.

After its original `a`-through-`y` uppercase pass, the exploration dispatcher
recognizes `D`, `V`, `Q`, `U`, `C`, `M`, `I`, `A`, `H`/`;`, `P`, `E`, `T`,
`W`, `O`, `#`, `@`, and Escape. `D` and `U` are contextual traversal keys;
`D` also drinks from the level-70 fountain when the player occupies its cell.
`A` offers to discard all carried treasure. `P` calls `7FFB`, which clears the
screen, displays `PAUSE... (Q FOR DOS)`, and waits for a key. Only `Q` from
that pause prompt saves and exits to DOS. The actual combat entry is `803A`,
reached by the occupied-cell/view path at `49B1`; the earlier combat label on
`7FFB` was disproved and corrected from direct control-flow and text evidence.

The U/D dispatcher changes the dungeon level by the signed 1..3 ladder value
and then calls `0CC0`. Its `4C28` floor initializer clamps the result below
zero to zero and above 70 to 70; it does not reject a valid ladder merely
because its span crosses an endpoint. False-floor value 25 remains outside
the accepted D range despite the misleading prompt described below.

## Dungeon resources and procedural walls

`7.NUM` is not a wall map. It is a 71-level by 21-row table of sparse 20-bit
special-cell masks BLOADed at DS:`8366`. Routine `5449` selects the mask for
`level * 21 + row` and extracts bit `20 - x`; only cells whose bit is set enter
the vertical-feature resolver at `54CB`. The supplied file has only 10 to 31
set cells per level, which is also incompatible with ordinary maze topology.

Routine `548B` computes ordinary walls procedurally using QuickBASIC MBF
single-precision arithmetic:

```text
INT(ABS(10 * SIN((((level + 2) / dungeon_generation_seed)
                  * factor_c * factor_b * factor_a) + 10)))
```

The QB3 `BCOM30.LIB` `sincos` module proves that BRUN selector `3D:39` is
`$SIN`: its public offset is exactly `45h` bytes from the runtime block that
maps to BRUN's entry at `BF0C`. Results 0 through 7 are traversable; 8 and 9
block movement. The persisted divisor is the dungeon-generation seed, not a
direction: character creation initializes it to 1, the save stream preserves
it after the dungeon level, and the fountain/restart path increments it by 2
to produce a replacement maze.

The four movement routines use these exact factor inputs and destinations:

| Direction | `factor_a` | `factor_b` | `factor_c` | Destination |
| --- | ---: | ---: | ---: | --- |
| North | `y` | `x` | 1 | `y - 1` |
| East | `y` | `x + 1` | 2 | `x + 1` |
| South | `y + 1` | `x` | 1 | `y + 1` |
| West | `y` | `x` | 2 | `x - 1` |

Arrow-key string dispatch proves the cardinal order. Each successful action
changes exactly one coordinate by one; the nearby action/speed counter is not
a movement stride. An active monster at the destination is tested before the
procedural wall value, but only while the view depth at DS:`B50E` is the
exploration value one. The movement routine does **not** enter combat: it uses
`COLOR 6`, `LOCATE 6,22`, prints `MONSTER BLOCKS WAY`, and returns without
changing the player coordinates. Combat begins only when monster movement
reaches the player. At the combat view depth of fifty the destination
occupancy test is disabled, allowing ordinary arrow movement to disengage
from the encounter.

A procedural wall or the fixed outer boundary merely prevents the coordinate
update. Neither branch owns a wall/boundary message string; the native port
therefore does not invent one.

Routine `340C` only clears the top notice area. The actual chute sequence
begins at `3428`. At `346D-3488` it uses `COLOR 1`, `LOCATE 1,1`, and prints
`YOU FELL DOWN A CHUTE!` followed by exactly 18 spaces, filling the first
40-column row. It performs no `TIMER` delay and no key wait, calls the save
routine immediately, and only then advances the dungeon level. It first
compares the current `(x,y,level)` with the last chute
landing at DS:`B4CE/B4D6/B4DA`; an exact match returns immediately and prevents
the new floor from triggering the same chute again. After the transition those
three globals receive the unchanged `x`, unchanged `y`, and new level.

The arithmetic between `34A0` and `355A` initially looks like a landing-point
relocation. Static data flow proves its first condition is
`INT((x+y)*.5)==0`, compared with the zero-initialized single at DS:`B7B0`.
Ordinary dungeon coordinates are positive, so that condition is always false
and execution jumps directly to `355D`; the apparent relocation tail is dead
for every reachable play cell. The native port therefore retains the same
coordinates rather than inventing a random landing point.

## Full-map raster compositor

The complete map redraw at `4CC3-53ED` iterates `x=1..20` outside and
`y=1..19` inside. A cell is considered only when its bit in the current
explored-map row is set. Its boundary origin is `((x-1)*8, y*8+37)`; its
seven-by-seven interior and player-background origin is `(x*8-7, y*8+38)`.

Each revealed cell always considers its east and south edges. Its north edge
is considered on row 1 or while the northern neighbor remains unrevealed, and
its west edge on column 1 or while the western neighbor remains unrevealed.
When that neighbor is revealed it owns the shared seam through its
unconditional south/east pass instead. Values 0 through 5 draw no internal
edge, 6 and 7
draw a passable edge with a black opening, and 8 and 9 draw a solid edge.
Horizontal door openings erase pixels `x0+3..x0+5`; vertical openings erase
`y0+2..y0+6`. Rows 1 and 19 and columns 1 and 20 remain solid boundaries
regardless of the procedural value.

The feature overlay at `52A2-5393` uses the same resolver as movement. A chute
draws a radius-three circle centered at `(x0+4,y0+4)`, a negative/upward
ladder draws an outline box from `(x0+2,y0+2)` through `(x0+6,y0+6)`, and a
positive/downward ladder draws the same box filled. Values greater than three
are suppressed, so the false-floor sentinel 25 is intentionally invisible on
the map until other game state replaces its hidden representation.

Player art is not a square. The initialized string descriptor at DS:`BA1E`
points to payload bytes `18 19 1A 1B` at DS:`BA22`, the CP437 up, down, right,
and left arrows. Startup code `049D-050C` prints and GETs rows and columns
0..6 from those BIOS SCREEN 1 glyphs. PUT dispatch `486D-48F4` maps facing
1/2/3/4 to up/right/down/left at the seven-by-seven player-cell origin. The
native compositor uses those exact cropped glyph rows. In color mode map
walls use DS:`19BC` = logical 2 (red), ladder boxes use DS:`19C8` = logical 1
(green), and the default-color chute circle plus captured town/player glyphs
use logical 3 (brown/yellow). In monochrome mode both stored colors and the
captured/default foreground are logical 3.

Floor zero adds a separate, statically recovered town-marker pass at
`C102-C331`. Startup prints `SBTIW` and captures the five SCREEN 1 glyphs as
individual GET buffers. The helper adds two to its supplied x coordinate and
one to its supplied y coordinate; its full-map caller first supplies
`(x0-1,y0)`, placing the final glyph crop at `(x0+1,y0+1)`. It chooses a glyph
from exact world coordinates:

| Glyph | World coordinates |
| --- | --- |
| `S` | `(18,3)`, `(13,18)`, `(2,8)` |
| `B` | `(13,3)` |
| `T` | `(7,15)`, `(14,12)` |
| `I` | `(7,3)`, `(3,2)`, `(18,17)` |
| `W` | `(6,14)` |

The source dispatch uses the original captured glyph buffers and PUT modes;
it is not a generic graphics-copy helper. The same routine is called both for
the expanded level-zero map and for the player's currently revealed town
cell.

The display-palette helpers are also exact. `2FF7` executes SCREEN 1 and then
applies the saved color palette only when color mode equals one. `300C`
submits `COLOR primary,secondary` twice through the QuickBASIC color runtime.
The intervening `XCHG DX,BX` followed by `MOV BX,DX` reconstructs the original
argument order after the first runtime call preserves BX and DX; it does not
swap the two COLOR arguments. Static BRUN30 analysis at `8DAE-8E19` proves
that SCREEN 1 masks the first/background argument to four bits, uses its bit
3 as the palette-intensity bit, and selects CGA palette zero/one from the low
bit of the second argument. The initial pair 0,2 therefore leaves a black
background with logical colors green, red, and brown, matching SCREEN 1
palette zero. The native port applies those registers directly instead of
using a fixed cyan/magenta/white palette.
The `#` command advances the first selector and wraps it after 16; `@`
advances the second through the original 2/3/4 cycle, applying 4 before
resetting the stored value to 2.

## Help-file reader (`C332-C5AF`)

Help uses SCREEN 0 at WIDTH 80 and reads `H1.OVL` through `H8.OVL` as
sequential records. A leading `~` is removed; `B9D6` advances the global
0..6 text-color counter before printing that record. Color mode's initialized
table maps those records to attributes 9 through 15, while monochrome maps
every table entry to attribute 15. Embedded byte 09 tab characters retain the BIOS
eight-column tab stops used by the continuation records.

Record 25 is printed without a trailing newline. When the active chapter is
not H1, the routine reads one nonempty key, clears the screen, resets its page
counter, and continues the same file. H1 instead leaves its 25-record index
visible. At EOF, `C47B` repaints the first character of H1 rows 1 through 16,
inserts highlighted `#` at row 13/column 3 and `Esc` at row 17/column 1, then
uses H1's last line as the chapter-selection prompt. Keys 1 through 7 open
H2 through H8; any other key restores SCREEN 1 and returns to play.

H8 also calls `C47B`: it repaints the first character of rows 1 through 13
and prints `Esc` at column 13 of the post-record cursor row. Every non-H1
chapter then advances the text color once more, centers the initialized
mixed-case `Hit any key` string at BASIC row 25/column 35, waits, and returns
through H1. The native port follows this control flow rather than imposing a
generic pause every 25 lines.

## First-person wall and door compositor

The renderer at `62E2-699D` draws into the active local `VIEW` rectangle
`(0,0)-(52,53)`. It iterates BASIC depth 1 through 6 using the paired inset
values loaded from F2.COM. The initialized perspective-wall color is logical
1 in color mode (logical 3 in monochrome) at DS:`19C8`, and door/fill color
is logical 2 at DS:`19C6`. BRUN30 `$LI6` confirms mode -1 is ordinary `LINE`, mode 0 is
`LINE ... B`, and mode 1 is `LINE ... BF`.

For each depth, a right or left vertical edge is drawn when that side at the
current depth, that side one depth farther away, or the center edge is at
least 6. The top crossbar is drawn on dungeon levels above zero, or when the
center edge is at least 6. Beginning at depth 2, side values 0 through 5 emit
the recovered rectangular inset; values 6 through 9 emit perspective
connectors. Side values 6 and 7 also emit a four-edge door polygon through
depth 5. At depths 2 and 3 the polygon is filled with BRUN30 `PAINT` from the
recovered interior seed.

The side-door projection columns in each F2 row are interpreted as:

```text
right: (a,53-e) -> (a,c) -> (b,d) -> (b,53-f) -> close
left:  mirror every x coordinate around x=52
```

QuickBASIC converts the fractional F2 endpoints through `$CINFAC`; static
runtime analysis at BRUN30 `A942-A947` shows positive halves round upward.
This matters for values such as 16.66667 and 23.75 and is preserved by the
native compositor.

A solid center value 8 or 9 draws the bottom crossbar and terminates the
depth loop. A passable center value 6 or 7 draws that crossbar, then a filled
door slab and terminates. With current inset `(x,y)`, the slab's near corner
is:

```text
inner_x = x + (52 - 2*x) / 3
inner_y = y + (53 - 2*y) / 3
```

and the opposite x is `52-inner_x`; BASIC depth 6 applies the recovered
one-pixel x offset. The wall compositor is implemented without DOSBox in
`mr_render_draw_first_person`, with deterministic tests for a solid face,
center slab, painted side door, QB rounding, and monochrome color selection.

The wall trace at `5AC0-6144` tests the center value before looking up an
occupant. Values 6 through 9 therefore stop visibility; doors are passable by
movement but opaque to this renderer. For an open value 0 through 5, the
occupancy lookup is one cell beyond the current center edge, so BASIC depth 1
means the adjacent cell rather than the player's cell.

Turning in place has a recovered incremental fast path at `580F-58F6`.
`580F` computes `(previous rendered cardinal direction - current facing) mod
4`; `5837` then repositions the four previously captured viewport bitmaps by
PUTting them at origin-table indices `rotation+1` through `rotation+4`. The
seven F2.COM origins are front `(214,57)`, right `(267,95)`, back `(214,137)`,
left `(161,95)`, followed by front, right, and back again so the four-entry
window can wrap without branches. Buffers at DS:`52F2`, `530A`, `58D4`, and
`5300` hold the front/right/back/left captures. The first and fourth PUT modes
are XOR; the middle two are PSET. If position or floor changes, the normal
four-ray renderer runs and refreshes these captures instead. Native
`MRViewportCache` preserves the distinct 53x54 front/back and 53x55 side
captures, the unchanged captured orientation, the seven-origin wrap window,
and the original XOR/PSET mode split. A full-frame fixture covers a north-to-
east cache rotation without executing the DOS binary.

Projected monster `PUT`s at `69FA-6B3B` use fixed, deliberately non-centered
local coordinates and rely on `VIEW` clipping:

| BASIC depth | bitmap family | family variant | local PUT x,y |
| ---: | --- | ---: | --- |
| 1 | large (125-word slot) | 0 | 11,25 |
| 2 | large (125-word slot) | 1 | 13,26 |
| 3 | compact (45-word slot) | 0 | 15,25 |
| 4 | compact (45-word slot) | 1 | 17,24 |
| 5 | compact (45-word slot) | 2 | 18,24 |

The combat-sized monster at the player's cell uses large variant one at
absolute `(225,112)` (`6C62-6C93`). Combat entry `78FF` first sets the render
direction to `(facing-2)` wrapped into 1..4, sets the single-view flag, and
tail-jumps into the renderer. That refreshes only the opposite/back pane,
resets `VIEW`, and falls through `6BAF` to overlay the large current-cell
monster. The native frame redraws all four panes before making the same
absolute monster PUT, preserving the final image while replacing this second
DOS-era partial-redraw optimization.

The surrounding SCREEN 1 text is also statically fixed; there is no general
level/coordinate/HP/SP HUD and no command legend. `4CDF-4D43` and
`4BC6-4C24` place `SPELLS` at `(5,35)`, `CAST` at `(6,35)`, `FRONT` at
`(7,28)`, `LEFT` at `(20,22)`, `RIGHT` at `(20,36)`, and `BACK` at
`(25,29)`, using one-based BASIC `LOCATE` coordinates. Floor zero adds
`YOU'RE IN TOWN` at `(5,1)`, while player levels below two add `H=HELP` at
`(18,28)`.

Combat setup prints `A LEVEL <level> <monster> IS ATTACKING!` at `(1,1)`.
The status block at `851D-85B1` places `YOUR HEALTH POINTS:` and the player's
current HP on row 3, `ITS HEALTH POINTS:` and the persistent monster HP on row
4, then places `EXP. VALUE:` at `(24,1)` and the reward at `(25,3)`.
`7F7D-7FC7` conditionally overlays `YOU FEEL VERY AGILE.`, `YOUR BODY
GLOWS.`, and `B-BREATH FIRE` at `(5,20)`, `(6,20)`, and `(7,20)` while their
TIMER expiries remain active. The native frontend now uses these recovered
coordinates and omits the earlier invented HUD text.

Routine `8FEA` is likewise not combat-specific. It returns unless
DS:`B4FA` says a command overlay was active; otherwise it clears the three
overlay rectangles, restores direction labels and status text, removes an
obsolete combat-pane image when the player cell is empty, and redraws the
current ladder/false-floor notice. Native command screens restore the complete
final dungeon frame, which is equivalent to these selective clears.

The native incremental-monster path must still copy its changed SCREEN 1
rectangles into the host frame before presenting. Merely redrawing the private
320x200 buffer leaves the previously presented panes on screen. It must also
not PUT the absolute `(225,112)` combat monster during this four-pane update:
none of the pane rectangles overlaps that sprite, and a second XOR PUT erases
it rather than preserving it.

The first GET-image header word is horizontal storage size in bits, not pixel
width. Because these resources target SCREEN 1, each pixel is a packed two-bit
CGA value: stored widths 72, 54, and 40 mean 36, 27, and 20 visible pixels.
Each row consumes exactly `ceil(width_bits / 8)` bytes: QuickBASIC
byte-aligns scanlines, while unused bytes in these fixed-size array slots
follow the complete image rather than padding every row. This makes the
recovered PUT positions center the art as expected without any native scaling
or inferred offset. Unqualified BASIC `PUT` uses XOR, so zero source pixels
preserve the corridor while nonzero two-bit source colors are combined with
it. The native resource decoder and compositor preserve all three rules.

## Vertical feature hash and paired ladders

Routine `5793` computes the feature hash for a marked cell. `$FEXC` is QB3
single-precision exponentiation and `$FSUG` subtracts the current FAC from a
saved expression temporary, so the exact expression is:

```text
raw = (x + 7)^1.3 * (y + 6)^1.2 * (level + search_offset + 1)^1.1
q = raw / 300
hash = INT((q - INT(q)) * 300 - 3)
```

All intermediate operations are MBF singles. Resolver `552B` interprets the
result as follows:

- a direct hash below zero becomes the false-floor sentinel 25;
- direct zero is a chute, except on level 70 where it becomes the no-feature
  sentinel 50;
- direct 1 through 9 is reduced to 1 through 3 by two conditional
  subtractions of 3, then `$FUMA` negates it: values -1 through -3 are ladders
  going up that many levels;
- for any other direct value, offsets 1 through 3 are tested with the same
  hash. A normalized candidate equal to its offset creates the corresponding
  positive/down-ladder endpoint on the shallower floor;
- failure to find a match becomes sentinel 50.

The level-zero candidate branch stores the compiler's canonical zero value
when the computed endpoint reaches the surface. This search-only path never
enters the direct-zero chute branch at `5552`; even an explicit entry into
`fall_down_chute` at `3428` returns immediately when the dungeon level is
zero. The resulting town circles share a real chute's numeric value and map
glyph but are intentionally inert. Map rendering suppresses the chute
transition while invoking this same resolver. Routine `56CC` prints the
original `Ladder going up.` or `Ladder going down.` notice from the sign of the
resolved value; routine `567C` prints `False floor.` and its `D-GO DOWN`
command hint. The used-false-floor controller at `064D-06CC` additionally
requires the saved landing X/Y, a current floor below 70, and either
`level == last_landing_level` or `level - 1 == last_landing_level`. The second
comparison is the explicit BRUN30 MBF addition of -1 at `06AA-06B2`, ORed
with the direct equality. That hint is an original bug: the actual D-command predicate at
`0DE0-0E1C` accepts only positive values below four, while the false-floor
sentinel is 25, so D silently does nothing. The native world/session tests
cover all six ladder signs, this rejected false-floor command, a positive-
floor chute, an inert level-zero circle, and no-feature cases
directly from `7.NUM`.

## Active effects

The active-effect table has physical base DS:`6020`; save element 1 starts at
`6024`.

| BASIC index | Address | Recovered role | Evidence |
| ---: | ---: | --- | --- |
| 1 | `6024` | battle Speed modifier | written by battle spell 4 at `9257-9295`; matched/cleared by successful movement at `0A65` and forcibly removed only on death at `A065` |
| 2 | `6028` | battle Strength modifier | written by battle spell 6 at `92C8-9303`; matched/cleared by successful movement at `0A8C` and forcibly removed only on death at `A083` |
| 3 | `602C` | preparation Strength modifier | preparation spell 3 at `36F4` |
| 4 | `6030` | preparation Speed modifier | preparation spell 4 at `3729` |
| 5 | `6034` | Invisibility effect state | preparation spell 10 at `38FA`; visibility checks at `7342` and `8F35` |

## Persistent value table

The 200-value table has physical base DS:`1B92`; save element 1 starts at
`1B96`.

| BASIC index | Address | Recovered role |
| ---: | ---: | --- |
| 1-3 | `1B96-1B9E` | knife, sword, and mace ownership |
| 4 | `1BA2` | disease state |
| 5 | `1BA6` | poison state |
| 10-11 | `1BBA-1BBE` | level-70 fountain X/Y coordinates |
| 12 | `1BC2` | Breathe Fire potion wall-clock expiry |
| 13 | `1BC6` | Shielding potion wall-clock expiry |
| 14 | `1BCA` | Agility/Speed potion wall-clock expiry |
| 17 | `1BD6` | owns the town |
| 21 | `1BE6` | race |
| 22-27 | `1BEA-1BFE` | blue, red, green, yellow, orange, and white pill counts |
| 28-36 | `1C02-1C22` | purple, brown, black, white, orange, yellow, green, red, and blue wand charges |

The temple proves that indices 4 and 5 are separate ailments: its two paid
branches clear `1BA2` and `1BA6` and print the disease and poison cure messages
at `2738` and `277D`. Monster attacks write the same fields. The general load
path resets indices 12 through 14 at `B99D`, making the combat potion fields
transient even though the generic save loop writes them.

The table audit is complete. Exactly fourteen instructions add the physical
base DS:`1B92`: twelve belong to the bounded pill/wand display, selection, and
loot paths and can reach only BASIC indices 22 through 36; `B56A` and `B929`
are the generic writer and reader loops bounded to 1 through 200. All other
accesses use a direct address already listed above. BASIC indices 6-9, 15-16,
18-20, and 37-200 therefore have no gameplay readers or writers. They are
reserved format capacity rather than unidentified features; the original
slots contain zero there and the native save layer preserves them verbatim.

## Items, wands, and pills (`1340-18DB`, `7AA1-7DC8`, `95BA-9A2C`)

`port/src/mr_items.c` is the native state-transition transcription and
`revenge_item_schema.json` is its machine-readable source record. Preparation
item slots 1 through 4 are Teleport, Seeing, Healing, and Spell Point scrolls.
Teleport writes the unusual intermediate half-floor value `.5` and
coordinates `(18,17)`, then immediately calls `1CF5`, which replaces the
floor with zero and recomputes the three town-entry combat factors;
Seeing writes `2^21-1` to each of the current floor's 21 reveal rows; Healing
fills HP; Spell Point adds exactly 10 SP. The Bag of Holding is a status-bit
item with 15,008 coin units of storage, while the reusable Floor Slosher works
through floor 40 and then prints `DOESN'T WORK THIS DEEP` without moving.

Combat slots 5 through 9 are Speed, Fire, Shielding, Health, and Relocation
potions. They add 13 Agility for 100 TIMER seconds, enable Breathe Fire for
100 seconds, set the shield threshold to 15 for 100 seconds, add 75 HP with a
maximum-health clamp, or choose each new coordinate as
`INT(RND*16)+3`. The Holy Hand Grenade enters the ordinary rewarded-defeat
pipeline. These branches return to the combat input point without giving the
monster a turn; relocation leaves the encounter and the grenade ends it.
Speed, Fire, and Shielding repaint their fixed active-effect fields. Health
prints the initialized dynamic string `You feel very good.` at BASIC row 18,
while the grenade prints `THERE'S AN EXPLOSION` on that row before entering
the reward pipeline; the other branches add no generic success banner.

The wand selector is deliberately reversed relative to the pill color table:
choices 1 through 9 are Purple, Brown, Black, White, Orange, Yellow, Green,
Red, and Blue. `7AE9-7B12` proves that order by printing color string
`10-choice` while reading persistent value `27+choice`. The nine colors
consume one charge before applying, respectively: floor `-1`, coordinates
`(10,10)`, HP `+25`, ten cannot-strike turns, a 240-point one-shot attack
bonus, free Strength, free Lightning, free Explosion, or full HP. Choices 6
through 8 still consume a charge outside combat and then report no effect. In
combat the wrapper first refunds the spell's cost before falling into the
ordinary spell routine, preserving both its effect and zero net SP cost.
White and Orange mutate those shared combat fields regardless of whether the
caller is already in combat, so dungeon use primes the next encounter. Only
Yellow, Green, and Red report `NO EFFECT` outside combat.

Pill colors 1 through 6 subtract two from attribute `(color+3)` wrapped over
six entries, then add four to attribute `(7-color)`. The red and orange pills
therefore change a single attribute by net `+2`; this is not a simplified
native rebalance.

The selectors have materially different source layouts and exit rules. The
preparation-item selector begins at `LOCATE 1,1`, prints `WHICH ITEM?`, then
the six fixed 21-character item records. An unavailable slot is its number
and `) ------------------`. The combat-item selector begins at `LOCATE 10,1`,
prints `WHICH ITEM:   `, a sixteen-space row, then the five fixed
`POTION OF ...` records and the Holy Hand Grenade; an unavailable record is
reduced to its leading digit plus `)------`. Their `L = LEAVE` branch exists
only while DS:`B55C` is one in the Wizard Guild information path. That wrapper
repeats invalid, out-of-range, and unavailable selections; preparation item
information accepts uppercase `L`, while battle item information explicitly
accepts `L` or `l`. Ordinary dungeon/combat use instead reads one raw key and
returns immediately to its owning command loop for a nonnumeric, out-of-range,
or unavailable choice. Escape and `L` consequently also leave ordinary use
through the generic `VAL(key)=0` path, though no leave line is displayed.

Wand callers first perform `LOCATE 6,1`; the pill routine performs
`LOCATE 10,1` itself. Neither menu contains an `ESC = LEAVE` line or a `)`
after its numeric choices. QuickBASIC's positive-number PRINT spacing makes
the records equivalent to ` 1 PURPLE 5  ` and ` 1 BLUE 5  `. A nonnumeric or
out-of-range key returns immediately. Selecting a numeric slot with fewer
than one charge/count also returns immediately rather than reprompting.

The `M` inventory routine at `3B16-3D82` is a two-page display. Its first page
checks, in order, Rings of Health, Bag of Holding contents, numeric magic sword
and mace bonuses, magic ring and armor status bits, Floor Slosher, Holy Hand
Grenades, and positive carried-item counts 1 through 13. Its second page lists
only positive wand-charge colors, then prints all six pill colors even when a
count is zero. Both page boundaries call the shared wait-for-any-key routine,
which renders mixed-case `Hit any key` at zero-based column 9 of row 24 before
reading a nonempty key. The pages do not change foreground color.

Player statistics at `19F7-1C75` is likewise a sequential 40-column PRINT
report, not a two-column dashboard. `Class: <race>` and the leading-space
` FIGHTER`/` WIZARD` record occupy separate rows; the six characteristics,
armor, owned weapons, HP, SP, level, weight, pocket money, experience, and
bank money follow in that order. Disease adds its two original lines at the
current cursor row. The same shared mixed-case wait prompt ends the report.
When DS:`B582` is one, the routine returns immediately after its armor/weapon
tail; `visit_store` uses exactly that branch to place the inventory summary
between `You are in the store.` and the store's money/purchase list.

The `A` command at `1918-19F4` waits four seconds on `You have no treasure.`
when the carried total is zero. Otherwise it accepts only Y/N. Confirmation
clears carried treasure and recomputes weight as 150 plus 25 pounds per armor
level; status bit 32 makes magic armor weightless. The branch then redraws and
opens player statistics. A negative response changes nothing.

## Monster identity and level derivation

Every dungeon floor has 40 persistent slots in `1.NUM`/`2.NUM`. For floor
`1..70` and slot `1..40`, the global BASIC index is:

```text
global_index = 40 * (dungeon_level - 1) + floor_slot
```

The combat and viewport paths independently derive the same base monster type
at `80B0-80DB` and `6BE3-6C0C`:

```text
base_type = global_index - INT(global_index / 20) * 20 + 1
```

This produces types 1 through 20. Types 19 and 20 are replaced by types 11 and
12 below floor 7. At floor 7 or deeper, persistent health above 140 instead
selects their extra variants 21 and 22. Monster level at `80DE-81A3` is:

```text
monster_level = INT(global_index / 40 + 1)
monster_level += (global_index is divisible by 2)
monster_level += (global_index is divisible by 4)
monster_level += (global_index is divisible by 8)
monster_level += (global_index is divisible by 16)
```

Thus the divisibility bonuses stack; they are not random difficulty rolls.
Replacement health written back to `2.NUM` after a defeat is
`INT(RND*dungeon_level*8)+dungeon_level*2+1` (`A3C8-A404`).

`C7A5-C8AC` loads complete monster resource sets. Floors 1 through 34 use
`F6.COM` and `3.NUM` through `6.NUM`; floors 35 through 70 use `F7.COM` and the
`3A.NUM` through `6A.NUM` variants. `4C6B-4CC0` swaps them immediately when a
level transition crosses 35.

## Active-monster movement (`70A1-78FC`)

Routine `70A1`, formerly given the provisional geometry label, is the
active-monster movement/occupancy updater. `6FBD` first decodes the current
monster's persistent position from `1.NUM`: X is the remainder modulo 32 and
Y is `INT(position/32)`. The routine then applies its weight, visibility,
distance, facing, and random-direction gates before testing one cardinal
candidate.

The overlapping constant annotation at DS:`B7EE` must be read as the
four-byte MBF-single value zero here, not as the eight-byte double value 15
which happens to begin at the same address. Consequently the tracking state
at `71C7-71D9` is zero when the monster is already on the player and one
otherwise; `7313-7322` clears tracking and pursuit when the monster is outside
the six-cell rectangular acquisition range. DS:`B69C` is also not a floor-slot
loop counter: `8061-806B` fills it from the occupancy entry on the player's
cell immediately before combat, making it the combat monster's global index.

The exploration movement logic, preserving BASIC's eager Boolean evaluation,
is:

```text
always consume RND for INT(RND*700)-400
if view_depth != 1 and combat_monster_index != current_global_index
   and that result > carried_weight: return
decode current x/y; return if already on player

tracking = (current position != player position)
if Manhattan distance < 4 and current == primary tracking slot: pursuit = 1
else if Manhattan distance < 5 and current == secondary tracking slot: pursuit = 1
else:
    clear either tracking slot which names current
    if abs(x-player_x) < 6 and abs(projected_y-player_y) < 6:
        tracking = 1
        consume RND for INT(RND*600)
        if that result < carried_weight: pursuit = 1
        else:
            always consume RND for INT(RND*10)
            if invisibility == 1 and that result < 5: do not acquire
            else acquire and install current as primary if primary is empty
    else: tracking = pursuit = 0

always consume RND for INT(RND*(monster_level+35))
if pursuit == 0 or that result < 15:
    direction = INT(RND*4)+1
else if tracking == 1:
    use player facing to choose the horizontal or vertical pursuit axis
else:
    choose an axis only when monster and player coordinates already align
```

The initial weight gate explains the original encumbrance warning: more
carried weight makes the `random-400 > weight` early return less likely, so
tracked monsters receive more opportunities to close. Invisibility is not a
blanket AI disable; it suppresses acquisition only when `INT(RND*10)<5` after
the weight-based acquisition test has failed. The `RND` calls participating
in compiled `AND`/`OR` expressions are still consumed when the other operand
already decides the Boolean result.

`0C0B-0CBF` advances the primary tracking monster after the player's position
changes and advances the secondary only when its global index is odd. The
`.5`, `INT`, and compare sequence is an odd/even test, not a random 50-percent
roll. `7001-70A0` uses the same parity idiom after advancing and wrapping its
floor-global monster cursor; the routine contains no graphics operation.

The four-view renderer does not populate those tracking slots. At `59F1`,
DS:`B66A` is reset to zero. A whole-module xref proves it has no reachable
nonzero assignment. Consequently `6CC5-6D3D` clears B506 whenever a projected
monster differs from B502; it never installs the first or second sprite as an
AI target. This original dead-depth-scratch quirk is retained by the native
port. Acquisition belongs exclusively to `70A1`, and the secondary slot has
no reachable nonzero writer in the compiled module.

Candidate coordinates are clamped to X `1..20` and Y `1..19`. The 22-column
current-floor occupancy grid at DS:`4E90` rejects an occupied destination.
For an accepted candidate, `76A5-76CB` swaps the old and new occupancy cells,
and `76CE-76EE` writes `new_y*32+new_x` back to the current global monster
slot in `1.NUM`. `758D-75D1` applies the same deterministic SIN-based wall
formula used by player movement and returns without moving when its integer
wall result is above seven. The player cell is deliberately not part of the
occupancy grid rejection: reaching it flows through `7718-78FC` into the
encounter/update path.

## Monster combat turn (`9A2F-A010`)

## Player weapon attack (`89D9-8E73`)

The native transcription in `port/src/mr_combat.c` also contains the complete
player attack path. Each attack begins with `INT(RND*20)+1` and accumulates
`INT(combat_attack_factor*.7)+roll+player_level`. A natural 20 repeats that
whole accumulation, so critical rolls explode rather than merely adding a
fixed bonus. Sword and mace then add their respective magic-item bonuses.

Monster defense is
`MIN(INT((monster_level-player_level)*.7),5)+5+INT(dungeon_level*.25)` plus
the monster's modifier and level. Against a fighter it is reduced by
`5+INT(player_level/6)`. Damage has strict score thresholds at defense,
defense+15, and defense+30. Maces are halved against behavior class 1, swords
against class 2, knives always halve damage, and fists reduce it to a third;
all preserve the original `INT` and `+1` ordering.

The wand-provided 240-point one-shot bonus has an original control-flow quirk:
it is added and cleared only after an attack already produces at least one
point. A complete miss preserves it. Positive attacks also consume a cosmetic
`RND` for prose before initiative is tested, which is retained so subsequent
random behavior remains aligned. Breathe Fire is the adjacent direct path and
does `INT(RND*30)+10` damage.

The complete native transcription is in `port/src/mr_combat.c`, with the
machine-readable derivation in `revenge_combat_schema.json`. The defense target
is `armor*2 + magic mace + magic ring + agility defense + 17`; fighters add
`INT(level/6)+5`, and the shield potion contributes a separate bonus.

One especially important original artifact is preserved: DS:`52FC` is both the
general numeric menu variable/player attack roll and the monster roll
accumulator. The monster code adds `INT(RND*20)+1` to its existing value instead
of assigning a fresh roll. It repeats that operation only if the resulting
value is exactly 20. Replacing this with a clean d20 roll would alter the
original game's hit distribution.

Damage uses strict attack-score thresholds at defense, defense+15, and
defense+30; adds independent level-scaled random terms even when the first
threshold fails; adds a further 18..66 points above monster level 60; and clamps
negative results to zero. A second strike is possible when
`INT(RND*(turn_speed_bonus+13))+1` exceeds player agility, but a guard permits at
most one repeat.

Recovered hit effects include the deep type-18 quarter-health `SQUASH!!`, class
5 level drain, deep type-14 health drain, deep class-5 strength drain, and deep
type-15 disease/agility drain. The death predicate is strictly level below zero
or health below zero, not less-than-or-equal.

The class-5 branch at `9E02-9E92` multiplies stored DOUBLE experience by
`0.7`; it does not clear experience. It then reduces player level by one and
rebuilds maximum health with the recovered drain roll. The apparently guarded
`SAVE` at `9E92` is reached for every drain because the compared `RND*10`
scratch is always nonnegative. That save occurs before the deep resource set's
following Strength decrement, an ordering now retained by the native session
snapshot.

## Defeat coin generation (`A561-A9FE`)

`port/src/mr_loot.c` transcribes all six coin-category rolls in their original
order: copper, silver, ivory, gold, platinum, and jewels. Copper, silver, and
ivory can only pass their level-biased equality test on floors zero through
three. Jewels require a floor deeper than three. The generated carry weight is
`INT((copper+silver+ivory+gold+platinum)/16)`; jewels contribute no weight.
Treasure value is
`INT(copper/100+silver/10+ivory*.5+gold+platinum*6+jewels)`.

The complete coin-and-extra-reward pipeline is entered for every even global
monster index, or for an odd index when `INT(RND*5)<>1`. This is an 80-percent
odd-index chance, not `=1`: the branch at `A54F-A55B` enters the loot code on
inequality. The gate's `RND` is consumed for even indices too because the
compiled `OR` eagerly evaluates both operands. If the gate fails, execution
jumps directly back to combat cleanup and skips every extra reward below.
Replacement health/coordinates are rolled before the kill message; the gate
is not evaluated until the player acknowledges the original `HIT RETURN`
prompt.

`A886-A88D` tests the computed integer treasure value, not whether any
individual category has a positive count. If that value is zero, execution
jumps directly to `A9FE` and the coin display is skipped. Thus a low
copper-only result can exist internally without producing a worthless
take/leave prompt. When the value is positive, the display offers the entire
result as one take/leave decision. On take, the computed weight is added to
carried weight and the value to carried treasure.
The pre-prompt 350-weight rejection contains a notable original variable-reuse
artifact: it compares carried weight plus DS:`52FC` (the shared menu/combat
roll), not the newly calculated loot weight at DS:`B76C`. The native UI must
retain that predicate for behavioral parity even though the actual take path
adds the proper computed weight.

## Extra defeat rewards (`A9FE-B2CE`)

`port/src/mr_loot.c` now transcribes the complete reward sequence following
the coin prompt, including its random-call ordering. A spellbook gate first
passes on `INT(RND*5)=1`. Its level is
`INT(RND*INT(dungeon_level*.5+1))+1`, rerolled while above level 6; the book
then adds bit 1 or 2 to either the preparation or battle mask. A duplicate
bit produces no book and is not rerolled.

Before floor 9, `INT(RND*8)=1` calls the fighter-only weapon routine. Rolls
1..3 advance ordinary armor one tier through tier 3, roll 4 grants a sword,
and the remaining/fall-through path grants a mace. Wizards return before the
routine consumes its inner random value. Monster behavior class 5 or 7 can
drop a 1..9-color wand when `INT(RND*300)<level+40`; class 5 can drop a
1..6-color pill when `INT(RND*160)<level+25`. Wands add exactly
`INT(RND*2)+1` charges.

The final `YOU FIND...` gate requires both
`INT(RND*dungeon_level+7)+1>10` and `INT(RND*5)+1=1`. It computes an
enchantment of `INT(INT(RND*dungeon_level)/3)+1`, then rolls 1..22:

| Roll | Reward |
| ---: | --- |
| 1 | Ring of Health; repeat rolls add another ring |
| 2 | Bag of Holding; a duplicate falls through to magic sword |
| 3..6 | magic sword, mace, ring, or armor upgrade |
| 7 | Holy Hand Grenade |
| 8 | Floor Slosher; a duplicate becomes `NOTHING` |
| 9..17 | original carried-item slots 1..9 |
| 18..22 | a fresh random book raises Strength, Learning, Wisdom, Health, or Agility |

Laziness cannot be changed by the book path. Magic sword, mace, and armor rewards
are unavailable to Wizards. Redundant or ineligible magic equipment prints
`NOTHING`. Both halves of each compiled BASIC `AND` consume their random call
even when the first condition is false; the native routine preserves that.
The carried-item announcement comes from F2.COM records 27..35 (loaded at
DS:`2142`), not the inventory-label table in records 18..26. The attribute
book descriptor order at `B03E-B068` is strength, learning, `wizdom`, health,
agility.
Magic equipment announcements preserve QuickBASIC's positive-number PRINT
padding: for example, the initialized `" A +"`, the value 7, and `"SWORD"`
render as ` A + 7 SWORD`.

The native controller now also preserves the presentation state machine, not
only these mutations. `A890-A953` clears the screen and prints only nonzero
coin categories in copper, silver, ivory, gold, platinum, jewel order. The
DS:`52FC` weight rejection prints `IT'S TOO HEAVY FOR YOU TO CARRY`, drains at
`A97C`, waits for one nonempty key, and never shows the `T=TAKE COINS
L=LEAVE COINS` prompt. After coins, the order is spellbook, shallow
weapon/armor, wand, pill, then generic reward. The first four use the shared
`2F3C` `Hit any key` helper. The generic path drains at `AC8D`, holds
`YOU FIND...` for four seconds, and drains again at `B0DA` before its final
acknowledgement. Attribute books additionally print `Press any key to read
it.`, wait once, raise the stat, print `You feel very good.`, and then reach
the common final acknowledgement. The source's `wizdom.` spelling is retained.
The coin screen restores the four cached view panes and combat-sized monster
after `CLS`; only the map and fixed labels remain blank. `YOU KILLED IT!!` and
`HIT RETURN` remain overlays at BASIC `(16,24)` and `(17,26)`. Generic rewards
normally print on row 2, while attribute books explicitly jump to rows 20-22.

The defeat handler begins at `A335` (the routine at `A2E5` writes a character
summary record and is not combat code). It awards pending experience as:

```text
INT(5 * 1.5^(monster_level^0.96)
    + 30 * (monster_level - 1)^1.4
    + 15)
```

Behavior class 2 explicitly multiplies that result by zero. The defeated
slot is immediately repopulated: health is rolled once as
`INT(RND*dungeon_level*8)+dungeon_level*2+1`, then `x=INT(RND*18)+2` and
`y=INT(RND*17)+1` are rerolled together until the cell is unoccupied. The
position stored in `1.NUM` is `y*32+x`. Every even global monster index
guarantees the complete loot sequence; an odd index receives it unless
`INT(RND*5)=1`. The native monster, combat, loot, and session modules preserve these
formulas and random-call boundaries.

## Inn progression and death

Combat experience accumulates in the pending field. The inn settlement loop
at `2094-2112` compares `experience+pending_experience` strictly against:

```text
900 * 1.2^((player_level^1.1)^1.1)
    + 180 * player_level^2.4 - 650
```

It does not subtract a threshold. For every threshold passed, `2044-2093`
increments the level and adds the same
`INT(RND*15)+health_growth_factor+1` roll to current and maximum health. After
the loop, pending experience is folded into stored experience and cleared.

`2115-21F2` recomputes spell points. Its aptitude is
`INT((Intelligence-12)/3 + Wisdom*.25 - 3)`, scaled by level with divisor 3
for Wizards or 6 for Fighters. The fixed class term is `3*level/2` for a
Wizard and `level-4` for a Fighter. A result below one is replaced by zero,
including a positive fractional result.

Death begins only below zero health or below zero player level. `A013-A22A`
rolls `INT(RND*4)+1`: 3 or 4 permanently removes the character. Otherwise a
second `INT(RND*2)+1` chooses reincarnation or a raise attempt. Reincarnation
sets level and experience to zero, rolls all six attributes as
`INT(RND*13)+3`, and adds ten to one randomly selected attribute. A raise
succeeds only when `INT(RND*23)+1 <= Health`; success reduces Health by one.
Both surviving paths restore current health, return to town at `(14,12)`, and
run the shared town-entry derived-stat routine at `1CF5`. Ordinary upward
traversal to floor zero, Teleport, town-warp spells, and the fountain use that
same routine; it is not specific to death. The exact flow is ported in
`port/src/mr_progression.c` and recorded in `revenge_progression_schema.json`.
The call at `A156` is the `2F3C` Hit-any-key/SCREEN-1 restore helper, not the
neighboring attribute clamp at `2F43`; however `2F3C` reaches `2F71`, whose
post-read side effect clamps every attribute back to at least one before the
town factors are recomputed. Health can therefore be transiently zero between
the raise mutation and its acknowledgement, but not when play resumes.

The outer transition at `A22B-A334` is now ported as well. Permanent death and
a failed raise remove the selected name from `F5.COM`, kill that numbered
`.EXE`/`.BIN` pair, rename every following pair down one slot, and write the
eight-field death summary consumed by F8. `B5C8` is not a character SAVE: it
prints `   Better luck next time!` when the death flag is set and BSAVEs the
two persistent monster tables. After the fixed 18-poll drain and generic
`Hit any key` acknowledgement, F8 inserts/sorts the summary, redraws its full
80-column ten-row table, and hands control back to BEGIN. Reincarnation or a
successful raise does none of that deletion work and resumes DUNSMALL in town.

`write_hall_of_fame_name_handoff_record` (`A2E5`) opens `NAME` for sequential
output and writes exactly eight values in this order: color-enabled flag,
player level, player class, selected character name, pocket money, bank
money, current dungeon level, and killer/status string. The ordinary quit
path calls it with an empty status string, which F8 converts to `STILL ALIVE`;
the permanent-death path supplies the monster name. This is the producer for
the typed F8 record, not a character-save routine.

## Fountain of Youth restart

The fountain command is offered only on floor 70 when the player coordinates
equal persistent values 10 and 11. Pressing `D` enters `3DAE-3F57`, prints
`YOU FEEL STRANGE...`, waits four seconds, and clears explored-map entries for
levels 1 through 70 and rows 1 through 20. Level zero and the row-zero entries
are deliberately not part of either compiled loop.

The next fountain position is independently rolled as
`INT(RND*15)+2` for both X and Y. Player level, experience, and pending
experience become zero, the player moves to `(10,10)`, and the dungeon seed is
increased by two. Pocket money is *replaced* by the double-precision conversion
of carried treasure, after which carried treasure becomes zero.

Maximum and current health are rebuilt before the attribute reward, using
`INT(RND*10)+health_growth_factor+5`. The factor is
`INT(Health*2-26)`, with the original `FIX(factor*.5)` correction when it is
below one. All six attributes then gain five, spell points are recomputed, and
the `1C93` routine clears invisibility plus any exact-one preparation Strength
or Speed marker and removes the corresponding `+6`/`+7` bonus.

An important persistence boundary occurs here: the character and explored map
are saved after setting the dungeon level to zero, but before `1CF5`
recomputes the three derived factors from the rewarded attributes. The native
progression/session transcription exposes that precise snapshot rather than
silently saving the final in-memory state.

## Spell dispatch

Preparation casting at `35AC` and battle casting at `90B4` each dispatch one
of twelve spells. The exact names, in dispatch order, come from `F1.COM`:

| # | Preparation | Battle |
| ---: | --- | --- |
| 1 | Cure | Gas |
| 2 | Sense Level | Magic Zot |
| 3 | Strength | Magic Bolt |
| 4 | Speed | Speed |
| 5 | Sense Location | Lightning |
| 6 | Descend | Strength |
| 7 | Feather | Go Away! |
| 8 | Ascend | Rise... |
| 9 | Change Level | Auto Kill |
| 10 | Invisibility | Explosion |
| 11 | Heal | Heal |
| 12 | Mocciolo | God? |

The masks store learned spells by level. Loot code at `A9FE-AB7F` chooses a
random level and one spell bit, checks the corresponding mask, then sets the
bit when a new spellbook is found. Both casting menus charge the selected
level: spell pairs 1/2 cost one point, 3/4 cost two, through 11/12 at six.

The complete chooser is shared at `C5D0-C7A4`. Preparation casting sets its
family selector to one and reads the mask at `5EAC + 4*level`; battle casting
sets it to two and reads `5EE0 + 4*level`. Mask bit one supplies the first
name and bit two the second; a missing bit produces a blank menu entry. The
menu prints `LEVEL <n>- SELECT ONE:`, those two names, and `3) CAST NO SPELL`.
Only `1`, `2`, or `3` are accepted, Escape is rewritten to `3`, and selecting
a blank learned-spell entry also resolves as choice three. The final
twelve-arm index is `2*level + choice - 2` in BASIC's one-based convention,
or `2*(level-1)+(choice-1)` in the native zero-based enum.

The level prompt itself is not a scrolling spell list. Preparation
`35AC-363F` uses `LOCATE 1,1`; it returns silently below one and prints
`NOT ENOUGH SPELL POINTS!!` on the third row for a value above six or above
the current spell points. Battle `90D3-9161` instead uses `LOCATE 10,1`;
values outside 1 through 6 return silently to the combat loop, while only an
insufficient-SP value prints the error at `LOCATE 12,1`. Both errors use the
same two-second TIMER hold. The shared selector retains the caller's base row,
so its four lines begin at row 1 for preparation and row 10 for battle. The
apparent occupancy shortcut at `C6C9-C710` requires both a monster on the
player cell and a zero view-depth value; normal exploration and combat use
nonzero view depths, so this is an inactive/guild-context guard rather than a
combat-spell cancellation. This menu contract is now represented by
`mr_spellbook_mask`,
`mr_spellbook_knows_choice`, and `mr_spell_dispatch_index`; the native game
controller calls those helpers rather than inferring ownership from names.

The preparation implementations are recovered directly from `36AA-3B01`:

| # | Exact effect |
| ---: | --- |
| 1 | `health = MIN(health_max, health + wisdom)` |
| 2 | Print the dungeon level |
| 3 | If inactive, Strength `+6` and mark the preparation effect |
| 4 | If inactive, Agility `+7` and mark the preparation effect |
| 5 | Print `x`, `y`, and dungeon level |
| 6 | Dungeon level `+1` |
| 7 | Carried weight `-250`, with the original Feather fall-through described below |
| 8 | Dungeon level `-1`; enter town at zero |
| 9 | Add `INT(RND*10)-5`, except a zero roll becomes `-1`; clamp to `0.5..70` |
| 10 | Set Invisibility and its visibility state to 32 |
| 11 | `health = health_max + INT(wisdom*0.5)` |
| 12 | One of six Mocciolo outcomes |

Mocciolo gives all attributes `+1`, warps to town, restores health and adds 30
spell points, warps to floor 50, gives all attributes `-1`, or loses two
levels. The last outcome quarters experience and computes:

```text
health_max = MAX(1,
    health_max - INT(RND*10) - INT(RND*10)
    - 2*health_growth_factor + 2)
```

The lower Change Level clamp really is the fractional value `0.5`; it is not
a decompiler artifact or a native-port correction.

The executable contains a genuine Feather fall-through at `37FF`: if removing
250 weight leaves a nonnegative result, execution continues into Ascend at
`3819`. If it went below zero, weight is clamped to zero and casting returns
normally. On floor zero the fall-through also reaches Ascend's no-op return
without deducting spell points. This is executable behavior, not an inferred
design correction.

Presentation is part of the recovered dispatch, not a generic result box.
Cure, Strength, and Speed enter the statistics routine; Sense Level prints
`YOU ARE ON LEVEL <n>` and uses the raw-key helper; Sense Location prints its
two coordinate/level lines and calls the four-second wrapper. Feather's
fall-through and Ascend use the compiled `POOF` transition. Mocciolo results
1, 5, and 6 alone print `WOW!`, `UH OH...`, and `Oh my God!`; the remaining
outcomes and duplicate persistent effects return silently.

## Command-cycle status and fountain presentation (`3D83-3DC0`, `4028-40F4`)

After the status redraw, `06D2-0841` consumes `INT(RND*7)` on every
positive-floor command cycle. Values zero, five, and six print nothing. The
other buckets conditionally print one exact advice line:

| Bucket | Condition | Text |
| ---: | --- | --- |
| 1 | `experience + pending_experience` is strictly above the current inn level threshold | `You should stay at an Inn.` |
| 2 | current health is below one quarter of maximum health | `You could use a cure!` |
| 3 | dungeon level is greater than `player_level*2+2` | `I don't think you'll survive down here.` |
| 4 | carried treasure is positive | `Go to bank to cash in treasure` |

The random value is consumed even when its condition fails. Omitting this
apparently cosmetic roll changes subsequent monster movement, combat, and loot
results, so the native session exposes it as a deterministic command-cycle
result rather than treating the text as optional flavor.

The level-70 fountain cell first consumes one `INKEY$` at `41A3`, resets the
arrow-lookahead counter through `4260`, then drains 18 more complete `INKEY$`
polls in `3D83` on every command pass. It overlays `You have found the
fountain of youth.` plus `Hit \`D' to drink.` Drinking clears the screen,
prints `YOU FEEL STRANGE...`, waits four
TIMER seconds, then applies the restart/save transition described above.

The status-and-map routine restores one HP per Ring of Health whenever current
HP is below maximum, capped at the maximum. A positive disease value is
incremented on that same cycle boundary. When the new value is an exact
multiple of 100, `INT(RND*6)` chooses one attribute, that attribute loses one,
`You feel sick. You need a cure disease.` is held for four seconds, and all
six attributes are clamped back to a minimum of one. DS:`B4C2` in this region
is compiler scratch reused as a redraw/regen gate and later as a map-math
temporary; it is not a stable persisted status variable.

The battle spell formulas at `91C2-9568` are:

| # | Exact effect |
| ---: | --- |
| 1 | Gas succeeds half the time against monster levels below 4, then deals `INT(RND*5000)+1376`; otherwise no effect |
| 2 | Magic Zot damage `(INT(RND*4)+1)*player_level+3` |
| 3 | Magic Bolt damage `INT(RND*27)+11` |
| 4 | Agility `+11`; set the cyclic transient marker to `(movement_turn == 1 ? 16 : movement_turn)-1` |
| 5 | Lightning damage `INT(RND*player_level*4)+player_level*2+5` |
| 6 | Attack factor `+7`; set the same cyclic transient marker for Strength |
| 7 | Go Away succeeds when `INT(RND*player_level*2)+5 > monster_level`, then enters the normal rewarded defeat path but suppresses `YOU KILLED IT!!` |
| 8 | Rise subtracts one dungeon level and leaves the monster alive |
| 9 | Auto Kill succeeds when `INT(RND*player_level*3)+5 > monster_level` |
| 10 | Explosion damage `INT(RND*player_level*4)+player_level*3+20` |
| 11 | Restore health to `health_max` |
| 12 | One of five God? outcomes: `INT(RND*5000)+1376` damage, town warp, full heal, no effect, or all attributes `-1` |

Battle Speed and Strength preserve an original stacking bug: recasting adds
the bonus again and merely overwrites the one marker. When the successful-step
counter later matches that marker, `0A4F-0AB0` therefore subtracts only one
`+11` or `+7` bonus even after multiple casts; leaving combat does not clear
either effect.
The fifth God? result has a separate compiled fall-through bug: it deducts six
spell points before lowering every attribute, then reaches the ordinary
completion path and deducts six more, for a total cost of twelve.

Presentation has three common spell exits. `9529` prints `NO EFFECT`, `9555`
handles successful Speed, Strength, Heal, and the fifth God? outcome, and
`9569` prints `YOU DO <amount> POINTS`; all three wait two TIMER seconds before
the monster response. Successful Go Away and Auto Kill, Rise, the God? town
warp, and the God? full-heal path bypass that hold. A successful Gas first
prints `IT FALLS ASLEEP AND YOU KILL IT` before the ordinary damage line.
Yellow, green, and red wands re-enter Strength, Lightning, and Explosion at
`8926-896A`, so they inherit those spell-result and counterattack boundaries.

The machine-readable version, including branch addresses, point-deduction
quirks, and random outcome flow, is `revenge_spell_schema.json`.

## Final static validation targets

- Keep the full-frame map, solid-wall, center-door, side-door and rotated-
  cache hashes synchronized only with changes justified by the decoded
  graphics instruction stream. These fixtures include QuickBASIC line tie
  cases and do not depend on DOSBox screenshots.
- Preserve the statically closed text path: BRUN30's active character vector
  at `DS:03B0+6` reaches `5F0E`, whose printable branch calls `272A`; that
  routine issues BIOS `INT 10h/AH=09h` with a count of one. The native renderer
  therefore uses the IBM BIOS 8x8 glyph generator and no host `.FNT` fallback.
- Keep the direct-launch NCD 80-column shareware pitch separate from the
  ordinary BEGIN handoff. BEGIN option five provably reaches only NCD's
  40-column order page.
