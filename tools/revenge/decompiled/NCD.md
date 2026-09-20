# NCD.EXE static reconstruction

This is an address-backed reconstruction of Moraff's Revenge Advanced 3.3's
historical ordering-information module.  It was produced from the executable,
its initialized data records, and the matched QuickBASIC 3 runtime only.  The
DOS program was not executed.

## Purpose and entry

BEGIN option 5 chains to `NCD` after writing `color_flag+109` to `NAME`.
`shareware_ordering_information_main`
(`0040`) opens the sequential `NAME` handoff and reads its first numeric
record.  Values 109 and 110 take the ordinary BEGIN path: NCD adds the compiled
`-109` constant, rewrites the restored zero/one color flag, and skips the long
sales explanation. Values below 99 take the direct-launch shareware pitch.
This mutation belongs to the historical inter-module handoff and is not
character-save data.

The first part selects the 80-column text layout.  At `02BA` the module
switches to 40 columns for the order instructions.  These are distinct source
layouts, not the same page scaled to two sizes.

## Explanatory pages

The exact 80-column text is emitted in this order.  Calls to
`ordering_advance_text_color_if_enabled` (physical `0521`, stored `0511` in
the expanded module) change the following paragraph's text color without
pausing:

1. `ORDERING INFORMATION`
2. A four-line explanation that the player has explored about one fourth of
   a dungeon and has roughly fifty more levels before the fountain of youth.
3. A three-line comparison of the beginner version's level-17 limits with
   the advanced version's level-70 dungeons.
4. Four lines listing deep-level wands, rings, pills, potions, books,
   spellbooks, and scrolls, including the undead pill-drop statement.
5. Five lines naming the Foot Stomper, Ogre, and Hobbit and describing their
   special attack behavior.

After the fifth paragraph, `020C-02A9` places `Please read this...` at row 25,
column 32 and waits until either a key arrives or 30 seconds elapse.  The
compiled `-30` and `10` constants implement the TIMER comparison and midnight
wrap.  It then performs sixteen nonblocking keyboard polls before erasing the
message and calling `ordering_prompt_and_wait_for_any_key` (physical `04ED`,
stored `04DD`).

## Forty-column order instructions

`02AE-045A` clears the prior layout, selects 40 columns, and prints one
25-row historical price/address/telephone page.  There is no intermediate
input wait on this path.  The address occupies rows 5-7, the color-segmented
telephone line occupies row 9, the existing-character note occupies rows
11-13, and the final two-line prompt is positioned explicitly at rows 24-25
(QuickBASIC's one-based coordinates).  It says that current characters remain
usable in the advanced version, that orders ship the same day, and that
orders include a free gift.

The final prompt is exact:

```text
If you  have a  printer,  turn it on and
   then hit `P'.

All orders include a useful FREE gift.

Hit any  other key to return to the main
   menu.
```

`ordering_read_nonempty_key` (physical `0505`, stored `04F5`) waits for a key.
Both uppercase and lowercase P select the printer branch at `0560`; every
other key chains to `BEGIN` at `04E7`.

## Printable order form

The printer branch uses QuickBASIC's printer-output statements rather than
screen `PRINT`.  Its exact record order is:

```text
Price:  US............ 10 US Dollars
        Canada.........13 Canadian Dollars
        Great Britain...8 British Pounds
        Australia......11 Australian Dollars
        Japan........1463 Japanese Yen

Price includes all tax and shipping charges even to foreign countries!

Please send  me the best game  available for IBM compatibles.  At this price,  I
     realize that I am getting  the bargain of the century,  and I agree to tell
     all of my  friends about it.  I understand  that I can  continue to play my
     current favorite characters when I receive my advanced version of  Moraff's
     Revenge.

____ A check is included and is made out to `MORAFF'.

____ Cash is included.

My address is:      ______________________________________

                    ______________________________________

                    ______________________________________

My phone number is: ______________________________________

Send to:     Moraff's Revenge                  or call *** 1-800-842-6858 ***
             815-A Brazos, #317
             Austin, TX 78701-2509

Please indicate disk size:       5 1/4 (Normal)      3 1/2 (PS/2, Laptops, Etc,)

Please describe your computer's configuration (optional):

     Computer:___________________________________________

     Monitor:____________________________________________

     Disk Drives:________________________________________

     Memory:_____________________________________________

     Printer:____________________________________________

     Modem:______________________________________________

Where you got this from:_________________________________

Comments (greatly appreciated):_____________________________________________

     _______________________________________________________________________
```

The module then returns to the same BEGIN handoff.  No game state, combat
state, or character record is changed by the printable-form path.

## Shared key/color subroutine

`ordering_prompt_and_wait_for_any_key` prints `Hit any key` at row 25,
column 35 and calls `ordering_read_nonempty_key`.  The separate
`ordering_advance_text_color_if_enabled` routine checks whether the handoff's
color flag is one, applies the current color value, increments it, and wraps
the sequence from 14 back to 9 when the next value reaches the compiled
sentinel 15.  The paragraph colors are deterministic and consume no random
numbers.
