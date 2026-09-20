# BEGIN.EXE static reconstruction

This is an address-backed reconstruction of the Moraff's Revenge Advanced 3.3
launcher. It is derived from the statically decoded QuickBASIC 3 module and
does not depend on observing the program in DOSBox.

## Module layout correction

`BEGIN.EXE` is the one analyzed game module using BRUN's expanded `bz` form.
The words following its `ON ... GOTO` runtime call at `03CC` are label offsets
relative to the payload at image offset `10h`. The five stored words are
`03CA`, `03F4`, `088B`, `09FA`, and `0A27`; their physical destinations are
therefore `03DA`, `0404`, `089B`, `0A0A`, and `0A37`. Each corrected address
begins a coherent menu arm. Compact `bm` modules such as DUNSMALL do not use
this adjustment.

## Startup and introduction

`begin_program_main` starts at `0040`. It seeds the QuickBASIC random generator
from `TIMER`, constructs the carriage-return and extended up/down key strings,
and reads the first numeric record of the inter-module `NAME` file
(`0040`-`00AC`). The value is not a video-mode number. It is the color flag,
with `10` serving as the initial-setup sentinel. Only that sentinel takes the
first-run branch at `00CE`; the player chooses color (`1`) or monochrome (`0`)
before the introductory pages are displayed. Other values proceed directly
to the menu at `0308`.

The first copyright page is literal 80-column text (`0111`-`017C`). BEGIN then
clears the screen, switches to 40 columns, resets its color-cycle variable,
and prints the role-playing, experience, and help prose (`017F`-`0305`). The
timed `Please read this...` loop lasts 15 `TIMER` seconds: ordinary `INKEY$`
values are consumed but do not bypass it, while the statically recovered
single-byte value at DS:`243E` is Ctrl-X (`18h`) and does. The line is then
replaced by `   HIT ANY KEY     ` and the common `begin_wait_uppercase_key`
routine at `0778` blocks for another key. That routine uppercases any one-byte value greater than `60h` by
subtracting `20h`, exactly as compiled.

## Five-way menu

The menu is printed in 40 columns at `0308`-`038E`: `MENU` begins at
`TAB(13)`, the five choices occupy the next five consecutive rows, and the
Ctrl-X note is placed at row 10, column 1. No `OPTION:` prompt exists. BEGIN
then accepts only a numeric value from 1 through 5 without redrawing the menu
for rejected keys (`0391`-`03CC`). Its statically recovered dispatch is:

| Choice | Physical arm | Operation |
| --- | --- | --- |
| 1 | `03DA` | rewrite `NAME` with the color flag and `CHAIN "CHCHAR"` |
| 2 | `0404` | load the roster, choose a character, rewrite `NAME`, and `CHAIN "DUNSMALL"` |
| 3 | `089B` | load and display the ten-entry `F9.EXE` hall-of-fame table |
| 4 | `0A0A` | write the current color state to `NAME` and terminate to DOS |
| 5 | `0A37` | write `color_state+109` to `NAME` and `CHAIN "NCD"` |

`NCD` is the shareware ordering-information module, not an installation or
directory utility.  The added 109 is an explicit inter-module marker. NCD
subtracts it, restores the original color flag in `NAME`, and skips its
direct-launch sales pitch before showing the 40-column order page.

## Character roster and selection

`begin_choose_character_and_chain_game` (`07AA`) reads the quoted sequential
roster `F5.COM` until its literal `END` record, retaining the name array and
entry count. The caller detects an empty roster and prints the original error
before returning to the menu (`0410`-`042A`).

For a nonempty roster, `042D`-`0574` prints each name, truncating names longer
than 35 characters to a 35-character prefix plus `...`. It is an 80-column
screen with the ten possible names beginning at row 3, column 1 and the help
text beginning at row 3, column 40. There is no added arrow cursor or scrolling
window. The current name is rewritten with foreground `normal+16`, which sets
the IBM text blink attribute, and color mode also gives it background 12;
monochrome uses background 0. The selection loop at `0577`-`06CA` has the
original wrap behavior:

```text
up from the first entry   -> last entry
down from the last entry -> first entry
Return                    -> accept
Escape                    -> return to main menu
```

The accepted path at `0815` identifies the highlighted name, writes the
color/selection handoff records to `NAME`, closes the file, and chains to
`DUNSMALL` at `0895`.

## Hall-of-fame display path

Choice 3 uses QuickBASIC `BLOAD` on `F9.EXE` (`089B`-`090D`) and reconstructs
ten fixed-width 80-character rows. It prints the two-column headings, ten
records, and the first five characters of every record as a compact rank list
before waiting for a key (`090F`-`09AB`). Updating and sorting this same table
after a death is performed by `F8.EXE`; see `F8.md`.

The two small color helpers at `09AE` and `09CA` are deterministic, despite
the latter's earlier provisional "random" label. When color is disabled they
return without changing the display. Otherwise `09AE` applies text color
`8 + color_cycle`, while `09CA` first advances `color_cycle` by one, wraps a
value greater than 6 to zero, and applies the same expression. No `RND` call
occurs on either path.

## Native-port requirements established here

- `NAME` is a temporary cross-module handoff file; it is not a character save.
- `F5.COM` is the ordered character-name roster and ends in `END`.
- The original roster selector wraps at both ends and recognizes extended
  up/down keys, Return, and Escape.
- Color selection is a binary flag after the one-time sentinel is resolved.
- Returning from character creation, gameplay, hall of fame, and ordering
  information is implemented by QuickBASIC `CHAIN`, so the native port must
  reproduce those state transitions without spawning another executable.
