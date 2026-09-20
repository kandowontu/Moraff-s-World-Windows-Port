# Moraff's Revenge native-port audit

> **Current status (2026-09-19): NOT CERTIFIED FOR 1:1 PARITY.** The earlier
> completion declaration and checked gates below were based on structural
> source coverage plus native tests whose expected values were often produced
> by the port itself. They did not prove branch, RNG-stream, raster, or timing
> equivalence. A clean-room re-audit has already invalidated that declaration
> by finding a SCREEN 1 GET row-layout error, incorrect combat ownership
> dispatch, two omitted startup RNG calls, and a reversed DUNSMALL color table.
> Historic checkmarks are retained only to show what the prior audit claimed;
> they are not current evidence of parity.

This document tracks evidence and implementation status for the native port of
Moraff's Revenge Advanced 3.3. It intentionally records behavior and formats,
not copyrighted game binaries or verbatim manuals.

## Live-path discrepancies corrected during the clean-room re-audit

- The inn level-settlement transcription computed a Wizard's base spell
  points as `3 * level + 2`. DUNSMALL `21BA-21CC` actually computes
  `3 * level / 2`, then adds the separately scaled attribute contribution.
  The original also clears every final value below one, including a positive
  fractional result. Both errors are corrected and covered by address-derived
  level-five and fractional-boundary fixtures.
- Flea Bag and Yuppydom call `3B02` immediately before their shared sleep
  routine, clearing invisibility and removing active preparation Strength and
  Speed bonuses. King's Inn intentionally omits that call. The port omitted it
  from both cheap inns; their original asymmetric effect cleanup is restored
  without changing King's behavior.
- Every level awarded by `2044-2093`, whether from inn experience settlement
  or a Temple purchase, also calls `1C93` after applying the HP roll. The
  native level helper skipped that cleanup, allowing preparation effects to
  survive a level gain. The shared level path now clears them at the original
  boundary.
- Native persistence retained the state snapshots taken at the original
  chute, fountain, level-drain, pause, and quit boundaries but omitted the
  `2FCB` drain that begins `save_character` at `B308`. Each pending original
  snapshot and the final normal quit save now performs those eighteen
  INKEY$ polls before writing.
- The native background-music wrapper queued QuickBASIC `PLAY MBX` but
  omitted the `05EE` call to the fixed eighteen-poll keyboard drain. Every
  successful original music selector reaches that boundary, including
  loading-review, inn, temple, and death music. The drain now lives in the
  shared native wrapper; sound-disabled early returns retain their separate
  original timing.
- The original does not freeze probabilistic floor-monster work while a
  combat or modal prompt owns the keyboard. The main combat INKEY$ loop at
  `86E2` calls `7EEC`, and the shared modal reader `7DC9` calls it for spell
  levels/choices, wands, pills, battle items, dropping treasure, the defeat
  Return, and both coin-loot branches. The native bridge previously used
  blocking host reads for all of those paths and rejected every non-exploring
  idle poll, shifting the RNG stream and freezing unrelated monsters. One
  normalized controller now covers the original polling boundaries; the
  session test includes exploration, town, and combat RNG cases.
- The native outer loop was running DUNSMALL's exploration/status routine
  (`3FFC`) once per combat prompt. The original combat loop (`803A+`) does not
  call it. This incorrectly regenerated health rings, advanced disease,
  consumed the ambient-advice random roll, and applied the exploration delay
  during battle. The core and live bridge now reject command-cycle status
  work in combat, with a regression test covering HP, disease, advice, and RNG
  state.
- The normalized empty-key loop updated a monster's coordinates but did not
  execute DUNSMALL `7749-78D4`'s immediate viewport redraw/cache replacement.
  A moved monster could consequently remain drawn at its old location. The
  bridge now redraws only the four dungeon panes and refreshes their cache on
  `MR_MONSTER_ADVANCE_MOVED`, preserving the map and queued top text.
- That incremental redraw still changed only the private 320x200 SCREEN 1
  buffer before presenting the unchanged host frame. It also XOR-drew the
  central combat monster a second time even though none of the four pane
  rectangles overlap its absolute `(225,112)` image, erasing it. The bridge
  now composes only the four changed source rectangles and never re-XORs the
  unrelated central sprite; a partial-compose fixture proves neighboring host
  pixels and text overlays remain untouched.
- A successful movement attempt during combat switched the native phase to
  exploration immediately. Original `09E8` skips exploration-only `0C0B`,
  then `8E76-8F67` can make the active monster pursue into the new cell and
  sometimes strike. The native path now preserves combat ownership through
  the exact fountain, behavior-three, armor-times-Agility, speed, invisibility,
  pursuit, and follow-up attack gates before deciding whether contact ends.
- The shared native town-choice wrapper uppercased every response. Static
  call-site recovery shows DUNSMALL's raw nonempty-key helper (`2F71`) is used
  by startup sound, inn, bank, temple, store, and Wizard Guild prompts, while
  the uppercase helper (`2F87`) is called only by the exploration (`0CD2`) and
  combat (`8704`) dispatchers. The affected modal prompts now retain the
  original uppercase-only choice behavior.
- The configured action delay was applied to dispatched commands, but the
  source owns its counter at a separate `4208` nonblocking `INKEY$`
  lookahead. That event is consumed before the `087F` dispatcher and only a
  queued arrow increments B5FA; empty/non-arrow samples reset it. The exact
  busy-loop bound is `INT(configured_delay/6)*B5FA` for counters through
  three, with larger counters bypassing the loop. The native bridge now uses
  that ownership and formula rather than a raw one-delay-per-command model.
- The live bridge changed phase as soon as a pursuing monster reached the
  player and therefore skipped the final `3FFC` status pass that precedes
  DUNSMALL's `49B1` transfer into `803A`. Combat entry now applies exactly
  one health-ring/disease status cycle and its possible four-second warning,
  but deliberately does not consume the ambient-advice RND that lives after
  `3FFC`. Subsequent combat prompts still perform no exploration status work.
- The pause presenter normalized its acknowledgement and accepted lowercase
  `q`. Original `8019-8025` calls raw `2F71` and compares only the uppercase
  `Q` literal; the normalizer applies to the dispatcher key that opened the
  pause, not to the response. The native pause now matches that distinction.
- The item selector had been documented and implemented as an original
  soft-lock workaround: ordinary use looped until an available choice and
  displayed an invented `ESC OR L = LEAVE`. Static branches `1449-16E3` and
  `968A-98E6` show the opposite. Ordinary preparation/combat use reads one raw
  key and returns on invalid, out-of-range, or unavailable input; only Wizard
  Guild information mode repeats and displays `L = LEAVE` (with its original
  prep-versus-battle case distinction). The selector and reconstruction notes
  now follow those actual branches.
- DUNSMALL's raw nonempty-key helper (`2F71`) is not merely an input wait. It
  unconditionally calls `2F43` after the read, clamping each of the six player
  attributes to at least one. The shared native reader now preserves that
  side effect at every session-bearing direct call, including help, pause,
  town, guild, item, book, and Sense Level paths.
- The bank's digit editor at `21F3-22F4` also enters raw `2F71` once for every
  accepted or rejected character. It had remained on the host's generic key
  reader after the first direct-call sweep, so its attribute-clamp side effect
  was still absent. The amount editor now uses the same recovered reader for
  digits, Backspace, Return, and ignored keys.
- The sequential writer correctly used QuickBASIC's `D` exponent marker for
  DOUBLE values, but the loader passed it unchanged to C `strtod`, which does
  not accept that marker. Large money or experience values could make a
  native-written save unloadable. Numeric input now normalizes only `D`/`d`
  exponent markers, with a full `1D+20` write-and-reload regression.
- The active-combat arm of the shared modal reader (`7DC9-7E47`) samples
  `INKEY$`, performs the floor-monster update, then discards that sampled key
  when the player cell is occupied and tail-calls raw `2F71`. The native
  reader incorrectly returned the sample; it now waits for the second raw
  nonempty key and applies the attribute clamp. The normalized native timing
  keeps `7DC9` in the explicitly adapted category rather than literal
  verification.
- The main dungeon reader also returned a buffered key before calling `7EEC`.
  Static control flow proves that exploration samples `INKEY$` first and then
  performs the monster/RNG poll (`087F`, `08F3`), while combat performs the
  poll first and samples second (`86E2`, `86E5`). Both execute one poll even
  on the pass that returns a key; town does not. The native controller now
  preserves those three boundaries and has a regression contract for their
  ordering.
- The equal-position gate inside the combat controller was read backward.
  `8707` resets DS:`B51C` to zero; `8783-878C` compares it with one and the
  not-equal branch enters the player's H/S/M/K/P/F/C/I/T/W/B dispatcher.
  The port instead sent ordinary contact directly to the monster turn,
  discarding every player attack key. The gate direction is corrected and
  locked by a focused regression.
- SCREEN 1 monster GET data is now guarded by logical-pixel hashes from both
  original resource sets and both compact/large shapes. These fixtures are
  original-resource-derived and catch the prior row-alignment corruption;
  whole-frame renderer hashes remain non-certifying because they are still
  generated by the port.
- The fountain command boundary was one poll short. `draw_status_and_map`
  consumes one `INKEY$` at `41A3` and resets B5FA before
  `handle_fountain_of_youth` performs the separately recovered eighteen-poll
  `2FCB` drain. The native controller now preserves all nineteen polls in
  their source order.
- The port had deliberately converted floor-zero vertical endpoints that
  overshoot the surface into `NO FEATURE`. DUNSMALL `55DD-561C` instead
  returns canonical numeric zero: the map draws the ordinary circular chute
  mark, but the floor-zero chute routine and `D` dispatcher both leave it
  inert. The original distinction is restored and covered by raster and
  traversal fixtures alongside a real filled-square dungeon entrance.
- The procedural maze used the host C library's `sinf`. BRUN30's shipped
  `$SIN` implementation is now recovered through `BF0C-BF67`: it reduces with
  the MBF constant at DS:`03CA` and evaluates the five-coefficient odd
  polynomial at DS:`06AA-06BD`. The native transcription now uses that exact
  reducer/polynomial. This is not cosmetic: at level 39 with maze seed 5 the
  host function produced solid wall value 8 where the original runtime
  produces passable door value 7.
- Vertical-feature generation, experience thresholds, monster rewards,
  Wizard Guild prices, and coin loot likewise delegated QuickBASIC `^` to
  host `pow`/`powf`. Static recovery of BRUN30 `B5B5-B89D` shows that `$FEXC`
  uses a range-reduced LOG2 rational approximation and a seven-coefficient
  EXP2 polynomial, rounding every stage as an MBF single. The shared native
  transcription now serves every recovered exponentiation site. Across the
  valid vertical-feature input space, host `powf` changed 1,800 raw hashes;
  marked floor-70 cell `(1,11)` is pinned at original hash 31 instead of 30.
  The same correction changes real high-level rewards (for example monster
  level 40 awards 5,980,042 rather than the host formula's 5,980,053).
- The session bridge inverted the original movement-occupancy condition.
  DUNSMALL only blocks an occupied destination while the combat view-depth
  value is one; exploration deliberately permits the step and enters combat
  from the next player-cell occupancy check. The native controller now uses
  that exact ownership, including the combat-only `MONSTER BLOCKS WAY` path.
- DUNSMALL's startup seed does not use a wholly initialized host integer: it
  preserves the recovered byte writes and consumes the same initial random
  values before review selection and dungeon play. The native startup path
  now mirrors that initialization rather than deriving a fresh host value.
- Combat separation formerly consumed the invisibility RND even after the
  armor-times-Agility branch had already escaped. The short-circuit at `8F2F`
  now preserves the original random stream.
- The `B4C2` redraw flag suppresses only health-ring regeneration inside
  `3FFC`; disease advancement remains outside that gate. Relative turns,
  procedural-wall attempts, coordinate boundaries, successful movement, idle
  combat entry, and movement-driven combat entry now retain their distinct
  status-cycle order without a duplicate combat-entry tick.
- Player-cell combat rendering used the nearest projected monster image
  (large selector zero). Static `6C62-6C7D` assigns selector one before the
  absolute `(225,112)` PUT. Combat, idle redraw, and reward reconstruction now
  all use selector one; original-resource pixel hashes cover both large slots.
- The attribute-address audit now explicitly accounts for QuickBASIC's
  unused physical element zero at DS:`1F92`. Direct address DS:`1FA6` is
  therefore gameplay attribute five (Agility, native index four), while
  Laziness is attribute six. Player initiative, repeat strikes, Speed spells and
  potions, and disease drains all consistently use Agility as compiled.
- The native numeric save writer no longer delegates record layout to `%g`:
  it emits QuickBASIC-shaped seven/sixteen-digit records, omitted leading
  zero, fixed/scientific thresholds, signs, and E/D exponents. Representative
  BRUN30-derived cases and every supplied save round-trip are covered, but
  exhaustive MBF decimal-rounding and B308 disk-error paths remain unresolved,
  so the enclosing writer is deliberately outside the certification count.
- Defeated-monster replacement cleared the old occupancy cell after rolling
  and validating its replacement coordinates. DUNSMALL `A3A7-A425` clears
  the old cell first, so the replacement is allowed to reuse it; the native
  order and the independent X-then-Y random consumption are now corrected.
- C does not define argument-evaluation order. The combat damage and Mocciolo
  transcriptions each embedded two stateful random calls in one expression,
  which allowed a compiler to reverse the original stream. Every multi-roll
  expression now samples into explicit temporaries in the recovered order.
- Successful raise/reincarnation survivors recomputed town factors before the
  acknowledgement read. The original performs `2F3C` first, including
  `2F71`'s minimum-one attribute clamp, and only then reaches `1CF5`. This is
  observable when a raise subtracts Health from one to zero; factor
  recomputation now occurs after the key/clamp boundary.
- The ordinary `build` executable predated the SCREEN 1 monster-row decoder,
  selector-one combat sprite, map-edge, and combat ownership corrections.
  Release validation now includes rebuilding that user-facing target rather
  than validating only the separate audit target.
- The statistics screen substituted `Luck` for the sixth F1.COM attribute
  label and rendered the lower numeric records as labels followed by values.
  DUNSMALL `19F7-1C92` instead loads `Laziness:    ### ` and builds the SP,
  level, weight, pocket-money, experience, and bank records by concatenating
  `############# ` before each label and passing the value through PRINT
  USING.  The original label and value-first field layout, including percent
  overflow, are now restored and covered by formatter fixtures.
- The native MBF-single encoders used `llround`, whose exact-half rule is
  away from zero. BRUN30's recovered mantissa conversion is nearest/even.
  The character-creation TIMER seed and BSAVE encoder now use the original
  tie rule, with fixtures on both sides of an even mantissa boundary.
- The four directional movement arms at `30D9-33E9` were re-read as complete
  routines instead of inferred from the shared input layer. Their exact
  facing writes, combat-depth-only monster block, greater-than-seven wall
  block, 1..20 by 1..19 coordinate limits, redraw/health-ring gate, successful
  movement-turn increment, and common status tail are now individually tied
  to native fixtures. The dispatcher at `30C7` is covered separately.
- The complete chute routine at `3428-35AB` was re-audited. It is automatic
  only after a successful step onto a positive-floor chute, prints without a
  wait, saves before increasing depth, retains x/y, and suppresses the exact
  landing triple on the new floor. The apparent relocation arithmetic is
  unreachable for every legal positive coordinate; the port does not invent
  a landing relocation.
- The port resolved vertical features for traversal and map glyphs but never
  presented the original current-cell notices. `567C` prints
  `False floor.` plus `D-GO/DOWN`; `56CC` prints the signed
  `Ladder going up/down.` plus U-GO/UP or D-GO/DOWN unless a monster occupies
  the cell. The false-floor gate also accepts both the saved chute landing
  level and the level immediately below it (`level == last` or
  `level - 1 == last`). Exact strings, SCREEN 1 text cells, occupancy gate,
  and both ladder signs are now recovered and tested.
- The level-zero town marker helper at `C102-C331` is now covered independently
  for all ten source coordinates, the recovered S/B/T/I/W crops, its +2/+1
  helper offsets, XOR/PSET split, and the three PSET inn markers.

## Certification gate

`tools/revenge/audit_revenge_parity.py` joins every function in
`revenge_source_map.json` to explicit records in
`revenge_parity_evidence.json`. Merely mentioning an address in native source
or passing a port-authored subsystem test does not upgrade a routine. The
default is `unresolved`; `adapted` records intentional native differences;
only an individually reviewed static contract can be `verified`.

Current gate result: **97 verified, 10 adapted, 60 unresolved out of 167
recovered routines. 1:1 certification is therefore NO.** Regenerate this
number with `tools/revenge/audit_revenge_parity.py`; the machine-readable gate
supersedes every historical checked list below.

## Authoritative source set

- Source container: fixed VHD supplied by the owner of the original game.
- VHD size: 7,340,544 bytes.
- VHD SHA-256:
  `9a4d18519f163b643e6cc16010846ecd16b1a7097c30073aebd6694b32d8c8cf`.
- Game directory: `Revenge` inside the VHD's FAT12 partition.
- Main version string: Moraff's Revenge Advanced Version 3.3.
- Copyright credit: Steve Moraff / MoraffWare, 1989.

The VHD is never modified. `tools/revenge/extract_vhd.ps1` and
`tools/revenge/analyze_revenge.py` reproduce the extraction and inventory from
any user-selected local path.

## Original program structure

| Program | Evidence-backed role | Port status |
| --- | --- | --- |
| `BEGIN.EXE` | first-run color setup, introduction and five-option main menu | statically disassembled; native setup sentinel, two-page introduction, five-arm controller and roster handoff wired |
| `CHCHAR.EXE` | character explanation, rolls, race/class, naming | statically disassembled; formulas/save initialization ported |
| `DUNSMALL.EXE` | town, dungeon, combat, inventory, spells, persistence | reachable code statically disassembled and function-inventoried; semantic and raster parity re-audit in progress |
| `F8.EXE` | hall-of-fame path | statically disassembled; native 80-column table/update/return path wired |
| `F9.EXE` | QuickBASIC `BSAVE` resource, not an executable | format identified and preserved as original data |
| `NCD.EXE` | shareware ordering information and printable order form | statically disassembled; exact reachable 40-column menu path and local printer-spool equivalent ported |
| `BRUN30.EXE` | Microsoft QuickBASIC 3 runtime | identified; never redistributed |

Five game files are DOS MZ programs compiled to QuickBASIC 3's mixed 16-bit
x86/selector representation and dispatched through `BRUN30.EXE`. The selector
byte following `INT 3D`, `INT 3E`, or `INT 3F` is consumed by the runtime, so a
normal x86 disassembler loses alignment. `tools/revenge/disassemble_qb3.py`
  decodes this representation directly; `disassemble_brun30.py` resolves and
  recovers the corresponding runtime handlers. DOSBox is not used for discovery
  or implementation. The port is derived from the decoded machine code and
  validated by native deterministic tests; any eventual DOS comparison is a
  separate, user-approved final verification step.

Current deterministic static recovery from the supplied binaries:

- `BEGIN.EXE`: 865 reachable instructions and 2,455 decoded bytes. Its
  expanded `bz` computed-branch words are adjusted by the statically proven
  `+10h` payload bias; the earlier unadjusted listing incorrectly treated
  inline table bytes as instructions.
- `CHCHAR.EXE`: 2,084 reachable instructions and 5,813 decoded bytes.
- `DUNSMALL.EXE`: code range `0030`-`C8AF`, 18,306 reachable instructions
  and 51,292 decoded bytes from the main entry/control-flow graph.
- `F8.EXE`: 519 reachable instructions and 1,404 decoded bytes.
- `NCD.EXE`: 653 reachable instructions and 1,963 decoded bytes. It is also an
  expanded `bz` module; correcting inline GOSUB targets by `+10h` recovers the
  clean `04ED`, `0505`, and `0521` helper entries. The native option-five path
  now follows BEGIN's `color_flag+109` handoff, NCD's `-109` restoration, the
  exact 40-column text, P/p dispatch, and the printable form record order.
- All five QB3 modules: 152 distinct runtime selectors observed.
- All recovered module instructions are now emitted in a separate lossless
  machine-readable graph. A second static pass groups the exact Microsoft QB3
  graphics handlers into compound operations. `DUNSMALL.EXE` contains 40
  complete `LINE` statements, six `LINE`-from-current continuations, one
  `CIRCLE`, two `PAINT`, 20 `GET`, 24 `PUT`, and 11 `VIEW` statements. Their
  setup instruction slices and executable addresses are preserved in
  `DUNSMALL.qb3.graphics.json`; no DOS execution or screenshot inference is
  involved.
- `BRUN30.EXE`: 165 total handler seeds (the distinct selector-table targets,
  its MZ entry point, and 14 nonzero PRINT-vector targets), 14,504 recovered
  instructions, and 33,486 bytes of reachable runtime code recovered
  statically. The entry path and selector-handler graph are decoded together;
  indirect vectors are not guessed from same-numbered file offsets because
  BRUN30's code and DGROUP address spaces are distinct.
- The ordinary text renderer is now closed statically. BRUN30 initializes
  DGROUP at image offset `0FE00h`; its active `DS:03B0` character-output vector
  selects `5F0E`. The printable branch calls `272A`, which sets `CX=1`,
  `AH=09h`, `BH` to the video page and `BL` to the color before reaching the
  runtime's BIOS `INT 10h` wrapper. The native port consequently renders the
  public-domain IBM BIOS 8x8 glyph data rather than Moraff's World `.FNT`
  assets or a visually approximated font.
- The QuickBASIC 3 distribution runtime is byte-for-byte identical to the
  game's `BRUN30.EXE` (SHA-256
  `b9ebf91c480d43093987b2e6dc6289fd59a9210fe1d8fc3c289ed7d022dffc60`).
  Static OMF matching recovers 740 Microsoft public runtime symbols from 106
  mapped `BCOM30.LIB` segments. Those names now annotate selector calls and
  BRUN handler listings without executing DOS code.
- Confirmed QB3 dispatch tables: `INT 3D` at `0171`, `INT 3E` at `0243`, and
  `INT 3F` at `038D` in the runtime core.
- DUNSMALL startup is now recovered through its first dungeon redraw. It asks
  `Sound (Y or N)?` on every invocation, consumes one value from the shared
  24-bit QuickBASIC random stream to select `REVIEW.1` through `REVIEW.6`,
  renders at most 24 sequential records in 80-column text mode, changes color
  when a record begins with a digit 1 through 7, changes the file's `|` marker
  to a comma, waits at `HIT ANY KEY`, restores SCREEN 1, and performs the
  standard 18-poll keyboard drain. The RNG consumption is retained because it
  changes the subsequent dungeon sequence.
- The native audio layer transcribes DUNSMALL's QuickBASIC 3 `PLAY` subset
  directly: tempo, octave, default/explicit lengths, dotted notes, accidentals,
  rests, articulation, and octave shifts. The exact temple, sleep, and death
  score strings at DS:`BA5E`, DS:`BAB8`, and DS:`BA7C` are queued in the
  original background mode. Sound-off preserves the original inn substitution
  of a four-second TIMER wait, including King's additional four-second wait.
- Town modal entry points now retain their `2FCB` 18-poll drains. The `O`
  command toggles the per-session sound flag, displays `SOUND ON` or
  `SOUND OFF` for exactly two TIMER seconds, then erases the nine-character
  notice, as compiled at `1055-10BA`.
- Battle spell presentation now follows the statically recovered three-exit
  graph at `9529`, `9555`, and `9569`, including exact text and two-second
  holds only on the paths that actually reach `2F1A`. The frontend now also
  presents the monster turn already calculated after a spell or spell-backed
  wand; the earlier bridge silently discarded that result. A defeated
  monster's combat frame remains visible through the result hold, matching the
  ordering before A4CA begins rewards.
- Floor transitions now follow the common `0CC0 -> 4C28` path: the result is
  clamped to the original 0..70 range, and any arrival at town calls `1CF5` to
  rebuild health growth, attack, and agility-defense factors. This also fixes
  Teleport's misleading compiled `.5` intermediate. Battle Speed and Strength
  now survive combat and expire only when the 1..16 successful-movement
  counter reaches their stored marker, including the original stacked-recast
  residual-bonus bug.
- `tools/revenge/extract_qb3_data.py` distinguishes both QB3 module layouts:
  exact contiguous initialized DGROUP images in `bm` modules and
  BRUN-expanded streams in `bz` modules. In a `bm` executable the second far
  call in the entry stub identifies the one-instruction module initializer;
  the initialized bytes begin at the next paragraph and their destination
  bounds are read from the module header. No scan heuristic or emulation is
  involved.
- Recovered initialized strings: 125 from `BEGIN`, 213 from `CHCHAR`, 386
  from `DUNSMALL`, 14 from `F8`, and 167 from `NCD`. Referenced MBF singles
  and doubles in the direct images are decoded at their exact DS addresses;
  eight-byte integral prices are no longer misidentified as four-byte zero.
  The
  DUNSMALL image maps file `C9F0`-`EB72` directly to DS:`B7D0`-`D952`; its
  SHA-256 is
  `fb778e88c02fac1ba44615beeb26182b2d23ec482b1cb5a93753f77244e5cd99`.
- QuickBASIC's compiled `READ`/`DATA` stream is also decoded statically. The
  original race/stat table is exactly Human `(4,4,4,4,4,4)`, Dwarf
  `(4,1,1,7,7,4)`, Elf `(2,6,5,3,5,3)`, and Hobbit `(2,2,2,5,9,4)`. Character
  creation then distributes `INT(RND(1)*10)+52` additional points one at a
  time among the six attributes. All four base rows total 24.
- BRUN30's random path is reconstructed as the original 24-bit LCG:
  `state = (state * FD43FDh + C39EC3h) AND FFFFFFh`. CHCHAR seeds it with
  `RANDOMIZE TIMER`. Native `mr_character.c` implements that generator, the
  race roll, derived combat/health values, starting money/health, class spell
  points, and initial persistent fields. Its deterministic formula test passes.
- Recovered computed dispatch: one branch table in `BEGIN`, thirteen in
  `DUNSMALL`, and direct-call procedure inventories of 6 (`BEGIN`), 6
  (`CHCHAR`), 126 (`DUNSMALL`), 8 (`F8`), and 1 (`NCD`).

## Original data formats

- `1.EXE` through `5.EXE` are sequential ASCII/CSV character records, despite
  their extension. Each currently contains 311 data records plus DOS EOF.
- `1.BIN` through `5.BIN` are per-character QuickBASIC `BSAVE` images. Their
  payload is 6,045 bytes plus the seven-byte `BSAVE` header and DOS EOF.
  DUNSMALL `B964-B97B` explicitly BLOADs every payload at DS:`9B06`, then
  treats it as the range through DS:`B2A2`, inclusive. The address words in
  the BSAVE header are therefore informational, not a compatibility gate:
  the supplied originals use offsets `9B06`, `4020`, and `4050`. The native
  loader now follows the explicit destination and preserves each header
  verbatim rather than rejecting valid characters with a non-`9B06` header.
  Its
  first 5,964 bytes are the 71-level by 21-row explored-cell grid: element
  `level * 21 + y` is a QuickBASIC single used as a 21-bit integer mask, and
  column `x` is bit `20 - x`. The final 81 bytes are fixed-endpoint padding:
  they occupy zero-initialized storage, have no static load/store use sites,
  and are zero in all five supplied sidecars. The port nevertheless preserves
  them verbatim so an imported original sidecar round-trips byte-for-byte.
- `.NUM` files beginning with byte `FD` are also QuickBASIC `BSAVE` images.
  The header records segment, offset, and exact payload length. Some paired
  files differ by display configuration.
- `1.NUM` and `2.NUM` are the two persistent 70-by-40 monster tables loaded at
  DS:`2242` and DS:`3824`; the first is the packed-position table and the
  second is persistent current monster health. The latter is proven by the
  load into DS:`B6E4`, the adjacent `ITS HEALTH POINTS:` display, and surviving
  combat writeback to the same table slot. `7.NUM`
  is the 71-by-21 sparse special-cell mask table explicitly BLOADed at
  DS:`8366`. Their complete file schema and address evidence are recorded in
  `tools/revenge/revenge_world_schema.json`; native `mr_world.c` loads and
  validates all three without executing the DOS game. DUNSMALL's ordinary
  walls do not use `7.NUM`: `548B` computes
  `INT(ABS(10*SIN(((level+2)/seed)*a*b*c+10)))`, and movement permits results
  0 through 7 while 8 and 9 are walls. The persisted seed starts at one and
  the fountain/restart path increments it by two, creating the replacement
  maze. The native helper preserves single-precision operation order and now
  uses the statically recovered BRUN30 `$SIN` range reduction and polynomial,
  rather than host `sinf`.
- The sparse marked cells are now resolved natively as well. DUNSMALL `5793`
  computes a modulo-300 hash from `(x+7)^1.3`, `(y+6)^1.2`, and
  `(level+offset+1)^1.1`; `552B` converts a direct 1..9 result into an upward
  ladder of length 1..3 and searches up to three deeper floors to reconstruct
  the matching downward endpoint. Zero is a chute, 25 is the false-floor
  sentinel, and 50 means no feature. Static inspection of `$FUMA` proves that
  direct ladder lengths are negated, while the notice routines at `567C` and
  `56CC` print `False floor.` and `Ladder going up/down.` respectively. Native
  tests cover the hash and every signed ladder length using the original
  `7.NUM` masks. Floor-zero exact reciprocal endpoints remain positive down
  ladders (filled squares, activated with `D`); candidates that overshoot the
  surface retain numeric zero and appear as the original inert circular
  chute marks. They cannot be activated on floor zero.
- `F5.COM` is a quoted sequential roster ending in the literal `END` record.
- `F6.COM` and `F7.COM` are the two original monster-name tables.
- Those two monster tables are not cumulative rosters. Floors 1-34 load the
  22 names in `F6.COM` with `3.NUM` through `6.NUM`; floors 35-70 hot-swap to
  `F7.COM` and `3A.NUM` through `6A.NUM`. The exact switch is at DUNSMALL
  `B941-B964` and `4C6B-4CC0`.
- `H1.OVL` through `H8.OVL` are the eight help chapters. DUNSMALL's exact
  SCREEN 0 / WIDTH 80 reader is now ported: leading `~` records advance the
  recovered color table, embedded tab bytes expand on eight-column stops,
  non-H1 chapters pause and clear at record 25, and their final page receives
  the centered mixed-case `Hit any key` prompt. H1 retains its own record-25
  selection prompt and repaints the source command initials plus `#`/`Esc`;
  H8 performs the corresponding combat-command repaint before returning
  through H1.
- `NAME` is BEGIN's sequential inter-module handoff. Its first numeric record
  is the color flag (`0`/`1`) or first-run sentinel (`10`); the play arm adds
  the selected character record before chaining to DUNSMALL. The native port
  preserves the first record in its isolated working copy and keeps selection
  in process.

The sequential character stream is now structurally mapped in
`tools/revenge/revenge_save_schema.json`: six individual attributes, three
mixed scalar records, three 30-element arrays, twelve paired spellbook masks,
and one 200-element persistent table, for exactly 311 records. The loader's
inline type list proves which scalars are QuickBASIC single or double values.
`parse_revenge_save.py` validates and names this stream without executing the
original. It reports both the stored record values and decoded gameplay values.
The exact load-time storage offsets are `237` (with a divide by three) for all
six attributes, `12316` for experience, `476` for player level, `376` for
maximum health, `176` for current health, `71` for carried weight, `4434` for
carried treasure, and `223` for pocket money. The constants were recovered as
MBF singles/doubles from the initialized data immediately following the
`2.NUM` string at DS:`D744`-`D76B`, then checked against all five supplied save
slots. All gameplay-reachable slots in the 200-value table and the 6,045-byte
`.BIN` sidecar are now classified.

The formerly unidentified 30-value block at DS:`B41E` is now proven to be
the magic-item value table. Its first seven saved elements are bag-held coins,
rings of health, magic sword and mace bonuses, magic ring and armor bonuses,
and holy hand grenades. The writer persists BASIC indices 1 through 30; the
physical element zero at `B41E` is not part of the save stream.

The same element-zero correction applies to the active-effect and 200-value
persistent arrays. Active-effect indices 1 through 5 are battle Speed, battle
Strength, preparation Strength, preparation Speed, and Invisibility. The first
persistent values now proven are knife/sword/mace ownership, disease, poison,
the level-70 fountain coordinates, Breathe Fire, Shielding, and Agility expiry
turns, town ownership,
race, six colored-pill counts, and nine colored-wand charge counts. In
particular, disease and poison are distinct fields: the temple cure branches at
`2726` and `276B` clear DS:`1BA2` and DS:`1BA6`, while the monster attack path
writes those same locations.

A complete reference audit finds fourteen computed additions of the table base
DS:`1B92`. Twelve are statically bounded pill/wand accesses to BASIC indices
22 through 36; the other two are the generic 1-through-200 save and load loops
at `B55E-B57B` and `B914-B939`. Every direct reference is one of the named
fields above. Consequently indices 6-9, 15-16, 18-20, and 37-200 are reserved,
not unresolved mechanics. The supplied slots store zero in them, while the
native writer deliberately preserves arbitrary values for format fidelity.

The native `mr_save.c` loader/writer implements the same transforms and passes
a byte-for-byte round-trip against the original slot 1 text and BSAVE files.
It also preserves QuickBASIC's noncanonical MBF zeros: an exponent of zero is
numeric zero even when stale mantissa bytes remain in memory, so those four
source bytes are retained until the corresponding explored-row mask changes.

Monster combat setup and turns are now transcribed in `mr_combat.c`. This
includes exact behavior-class assignment, the original shared-roll variable
artifact, armor/ring/mace/agility defense, fighter defense bonus, all three
damage thresholds, level-scaled damage, the one-repeat agility check, and deep
monster level/health/strength/agility/disease effects. Deterministic tests use a
scripted RND stream so branch order and random-number consumption are checked
without running DOSBox.

A follow-up runtime-operator trace corrected the class-5 level drain at
`9E02-9E92`: `$FMUB` multiplies the DOUBLE experience field by the recovered
MBF `0.7` constant rather than assigning zero. The same branch's seemingly
conditional SAVE is unconditional for its nonnegative `RND*10` scratch and
occurs before deep-set Strength loss. Native combat and session tests now lock
both the arithmetic and this persistence boundary.

Inn level settlement and the complete death lottery are now transcribed in
`mr_progression.c`. This includes the nested experience-threshold exponent,
one shared HP roll per level, class-specific spell-capacity recomputation,
permanent-death/reincarnation/raise probabilities, the Health raise check,
and the intentionally harsher derived-stat recalculation used after surviving
death. The native deterministic test exercises these paths without starting
the original program. The DUNSMALL controller now also follows the recovered
`A013-A334` outer transition: reincarnated/raised characters resume the same
session in town, while permanent death or a failed raise rewrites `F5.COM`,
deletes the selected numbered pair, shifts every following `.EXE`/`.BIN` pair
down one number, preserves the persistent monster tables, records the actual
shallow/deep monster name in F8's fixed-width summary, and returns through the
hall of fame to BEGIN. A dead character record is never written back over the
deleted slot.

Town mechanics are now statically reconstructed in
`tools/revenge/revenge_town_schema.json` and transcribed in `mr_town.c`.
This covers all three inn prices, healing, robbery and sickness rolls; bank
treasure exchange and capped transfers; all five temple prices/effects and
its charge-before-no-effect quirk; all seven store items, class/upgrade checks,
and the mismatched Town visibility/purchase thresholds. DUNSMALL `10FD-12C5`
also proves that town is the ordinary floor-zero maze: seven service IDs are
selected from exact player coordinates, `12EE-1327` advertises a rope, and
`U` dispatches through the recovered `132A` ON-GOTO table. The native session
now permits floor-zero movement and wires those coordinates to the three inns,
bank, temple, store, and wizard guild.

The Flea Bag's separate ten-percent sickness branch removes one Health
characteristic before setting disease, then calls the recovered `2F43`
six-attribute minimum-one clamp. Pills and all three deep-monster
characteristic drains share that same clamp at their exact post-mutation
boundaries.

The bank now also reproduces the load recomputation at `231F-2355`: after
cashing treasure, carried weight is 150 plus 25 per ordinary armor level,
while magic armor is weightless. Wizard-guild magic-item information is the
fixed 800 JP path, but spell information is a separate formula recovered at
`2DAC-2DC3`: `INT(220 * level^1.75)` for levels 1 through 6. Both only charge
after a nonzero nested P/B selection. The deterministic native town test and
interactive service controller run without launching DOS or DOSBox.
Store and wizard-guild re-entry now also pass through their recovered `281E`
and `2BBE` fixed drains on every repeat menu iteration; a held purchase or
category key therefore cannot leak into the next prompt.

The original input dispatcher is now reconstructed in `mr_input.c` with a
deterministic test. It includes both Escape-toggled arrow schemes, direction
wrapping, the movement-only turn increment, the original `a`-through-`y`
uppercase quirk, the fixed 18-poll keyboard drain, two/four-second message
delays, and the configured 0-to-3000 action-delay bound. Static evidence is in
`tools/revenge/revenge_input_schema.json`; no DOSBox observation was used.
The same pass recovered the complete exploration command table and corrected
a false early label: `7FFB` is the pause/Q-for-DOS routine, while combat begins
at `803A`. The generated source map and annotated listing now preserve that
distinction.

A second static input/timing pass closes several controller-level gaps which
were invisible to formula-only tests. Combat invokes ordinary arrow movement
at `871F` before dispatching attack keys and leaves contact through `8E76` when
the player successfully separates from the monster. Weapon output is held for
exactly one QuickBASIC `TIMER` second by `8E12-8E34`. Monster output uses the
two-second helper plus the 18-poll drain for SQUASH and attribute/status
notices (`9DBB`, `9EE3`, `9F14`, `9F40`, then `9F8E-9F94`) and a final
one-second hold at `9FEC-A00E`. The native presentation now stages the player
and monster halves of an attack in that order rather than resolving both and
immediately returning to input. Defeat uses the literal `YOU KILLED IT!!` and
accepts Return only after its drain (`A4D8-A520`); the coin T/L prompt has its
own drain at `A997`, and the generic `YOU FIND...` teaser uses the original
four-second wrapper at `AC8D-ACA2`. The shared SDL queue now exposes a
complete-DOS-key fixed-poll drain so a zero-prefixed arrow counts as one
QuickBASIC `INKEY$` string and cannot leak its scan byte into the next prompt.
The `S`/`M`/`K` ownership gates at `87CA-8847` are also enforced before the
attack formula; an unavailable weapon prints the original two-line
`YOU DO NOT HAVE THAT / WEAPON!!` rejection without spending a combat turn,
while `F` always selects fists.

The post-combat presenter is now a direct transcription of `A890-B2CE`, not a
single native summary. It clears and lists only positive coin categories in
the original copper/silver/ivory/gold/platinum/jewels order, suppresses T/L on
the DS:`52FC` too-heavy branch, and then presents the independently gated
spellbook, shallow equipment, wand, pill, and generic reward screens in
source order. Each spellbook/equipment/wand/pill screen reaches the original
`2F3C` `Hit any key` boundary. Generic drops retain the pre-teaser drain and
four-second hold, print `NOTHING` for rejected upgrades, and preserve the
attribute book's separate `Press any key to read it.` acknowledgement followed
by `You feel very good.` and the final drain/acknowledgement.

The first cross-module native program layer is now in place without invoking
the DOS executables. `mr_frontend.c` reads BEGIN's quoted `F5.COM` roster and
literal `END` sentinel, implements its wrapping selector, decodes F8's ten
80-column hall-of-fame rows from the exact `F9.EXE` BSAVE word layout, formats
the original fixed fields, and preserves the remaining payload when writing.
`mr_session.c` combines the independently recovered save, world, input,
combat, loot, and monster modules into a headless DUNSMALL state machine. It
rebuilds the 22-column floor occupancy grid, reveals the current map bit,
checks destination monsters before procedural walls, enters combat, applies
the combat/loot path, and respawns a defeated persistent slot with the
original health/coordinate RND order. A thirteenth and fourteenth deterministic
test suite cover these cross-module paths.

`mr_game.c` now exposes that recovered program boundary through Moraff's
World's character menu. Its native launcher preserves BEGIN's numeric 1..5
dispatch, F5 roster selector (two-byte Up/Down, Return and Escape), numbered
`N.EXE`/`N.BIN` handoff, F9 hall load, exact 80-column F8 headings/rows and
highlighted level prefix, and explicit return to the World menu.
Character creation calls the statically reconstructed CHCHAR roll and save
initializer instead of borrowing Moraff's World's unrelated creator. It now
also presents the two recovered 80-column explanation pages with their exact
9-through-15 text-color state machine, then switches to the original
40-column inverted-highlight race selector and consecutive roll rows. The
accepted race heading remains visible while class, knife, and name input append
below the roll. Color mode consumes the recovered cosmetic RND call for every
race before money/health generation; monochrome does not. The controller keeps
the Left/Right wrapping selector, retains a roll across invalid Y/N input,
labels the sixth characteristic Laziness, seeds through QB3 `RANDOMIZE TIMER`,
applies CHCHAR's exact byte-greater-than-92 name normalization, reproduces
duplicate-name replacement, and returns directly to BEGIN after the save write
without an invented success modal. The
first interactive DUNSMALL controller drives the native session, traversal,
combat, defeat acknowledgement, all-or-nothing coin prompt, respawn and save
writer. A seventeenth deterministic suite checks the BEGIN-to-DUNSMALL bridge
without starting DOSBox.

The session layer also wires the statically recovered preparation and battle
spell, item, wand, and pill routines into their original state transitions.
`RISE...` leaves its monster alive, successful `GO AWAY!` suppresses only the
kill wording while retaining the Return boundary, battle consumables return
without a monster counterattack, and the colored-wand cannot-strike and
one-shot damage globals survive encounter boundaries. The inactive shielding
timer restores the original, superficially odd, defense value of 16. These
behaviors are deterministic transcriptions of DUNSMALL rather than screen
observations.

The interactive spell picker is now also translated from executable control
flow rather than redesigned. DUNSMALL first accepts one level key from 1 to 6,
rejects a level beyond the available spell-point total, and then exposes the
two names selected by that level's learned-mask bits. Escape and a blank
unlearned choice both resolve as `CAST NO SPELL`; the twelve-way dispatch is
`2*(level-1)+(choice-1)`. Native helper tests cover the exact bit and index
mapping, and both exploration `C` and combat `C` use that shared selector.
Preparation-spell presentation is likewise address-backed: Cure, Strength,
and Speed enter the statistics screen; Sense Level waits for a raw key; Sense
Location holds its two-line coordinates for four seconds; Feather/Ascend use
the compiled `POOF` transition; and only Mocciolo outcomes 1, 5, and 6 print
`WOW!`, `UH OH...`, and `Oh my God!` respectively. Duplicate effects and the
other transition spells receive no invented native confirmation text.

The remaining decoded exploration inventory commands are now connected as
well. `M` follows DUNSMALL `3B16-3D82`: its first full-screen page preserves
the executable's conditional magic-item order and original 13-slot carried
item limit, while its second page suppresses empty wands but prints all six
pill counts. `A` follows `1918-19F4`, including its four-second empty-treasure
message, strict Y/N loop, magic-armor weight exception, body-plus-armor weight
recalculation, and statistics follow-up. Pure snapshot/mutation tests protect
these rules independently of the SDL presentation layer.
The same pass restores the floor slosher's precise boundary: below its usable
depth it prints `DOESN'T WORK THIS DEEP`; on success it prints `YOU ARE
SLIPPING THROUGH THE FLOOR.`, waits for one raw key, then changes floors.
Yellow, green, and red wands used outside battle print `NO EFFECT` for exactly
two TIMER seconds, and inventory page two exits through its fixed drain.
White and Orange are different: `7C3C-7C48` updates their shared cannot-strike
and one-shot-damage counters even outside combat, deliberately priming the next
encounter. Successful utility items, pills, ordinary ladder attempts, and the
Escape movement-mode toggle are otherwise silent. Healing and Spell Point
scrolls enter the sequential statistics report; Floor Slosher uses the shared
row-25 `Hit any key` prompt. Combat Health prints the initialized mixed-case
`You feel very good.` at BASIC row 18, and the Holy Hand Grenade prints
`THERE'S AN EXPLOSION` there before the normal reward flow.

The level-70 Fountain of Youth path is now recovered and transcribed as a
complete session transition. It clears only explored levels 1..70 and rows
1..20, rolls the next fountain coordinates independently, replaces pocket
money with carried treasure, rebuilds HP before awarding five points to every
attribute, advances the dungeon seed, clears the exact preparation effects,
and returns to town. The native session also exposes the original persistence
ordering: both a chute and the fountain invoke SAVE before their subsequent
level/derived-stat mutations. The disassembly source map now correctly names
`1C93` as preparation-effect cleanup and `B2CF` as fountain-position
randomization; neither is a visual inference.
At the fountain cell the command loop drains held input and overlays the exact
`You have found the fountain of youth.` / `Hit \`D' to drink.` prompt on every
pass. Drinking clears the screen, displays `YOU FEEL STRANGE...` for four
seconds, and only then applies the recovered restart transition.

DUNSMALL's per-command status work at `4028-40F4` is now in the headless
session layer. Rings of Health restore their stored ring count up to maximum
HP on each status/redraw cycle. A positive disease counter advances on the
same boundary; each exact multiple of 100 consumes one RND value, reduces one
of the six attributes by one, clamps every attribute to at least one, and
holds `You feel sick. You need a cure disease.` for four seconds. Tests lock
the mutation and RND boundary without executing the DOS program.

The defeat transition now retains the original input and RNG boundary: the
replacement monster is generated immediately, `YOU KILLED IT!! / HIT RETURN`
is a distinct session state, and only its acknowledgement consumes the eager
loot-gate RND. Static branch recovery at `A528-A55E` corrected an earlier
transcription: even global indices always enter the complete reward pipeline,
while odd indices enter when `INT(RND*5)<>1` (80 percent). A failed gate skips
coins and all extra drops. Coin results are a single all-or-nothing T/L choice,
including the original DS:`52FC` variable-reuse bug in the 350-weight check.

The global monster-table writer in `mr_world.c` emits the original partial
2,800th-element BSAVE ranges for `1.NUM` and `2.NUM`. Native saves will use
isolated copies seeded from the user's original resources rather than
modifying the installation-wide tables. `mr_game.c` now creates the relative
peer directory `revenge-native` on first entry, copies `F5.COM`, `F9.EXE`,
`NAME`, `1.NUM`, `2.NUM`, `7.NUM`, and every existing numbered character pair once,
and directs every later roster, hall, character, and monster-table write to
that working set. Static help, layout, review, data, and art remain loaded
from the immutable `revenge` resource directory. A later native deletion is
not repopulated from the originals on restart. The initial import is
transactional: individual files are renamed from temporary copies and the
`.seed-complete` marker is written only after the complete working set has
been seeded.

The five direct calls to DUNSMALL's `B308` character writer are now accounted
for statically. Main-screen and pause-menu `Q` save the current record; chute
and fountain transitions and the class-five level drain expose their exact
pre/post-mutation snapshots. Native gameplay flushes those snapshots at the
recovered command boundary rather than postponing them until the session is
closed. This preserves the original pre-floor-increment chute save and the
pre-Strength-decrement level-drain save.

The positive-floor command loop's seven-way ambient-advice roll is now also
stateful rather than decorative. The exact inn/cure/depth/bank predicates and
strings are ported, and the otherwise silent buckets still consume their
QuickBASIC `RND`; this closes an RNG-boundary drift that would otherwise alter
all later monster and reward rolls.

Static recovery also separated three timing mechanisms that must not be
collapsed together: DOS BIOS typematic repeat, the user-entered 0..3000 busy
delay, and DUNSMALL's own one-second monster-update calibration. `BF60-BFB9`
measures tight-loop iterations and divides by 326; `7EEC-7F42` uses that
factor in `MAX(INT((165-monster_level+player_level)*factor/20),8)` and invokes
`7001` when `INT(RND*interval)=1`, even while `INKEY$` is empty. `7001` is not
a sprite routine: it advances/wraps the floor-global monster cursor, selects
that monster record, and enters pursuit only for an odd global record. The
native input layer now keeps this path separate from typematic: it schedules
326 normalized empty-key polls per second, consumes one original RNG value per
poll, preserves the cursor/parity gate, and enters combat immediately when an
idle monster reaches the player's cell. Deterministic session tests cover the
interval formula and both parity branches.

The monster graphics are decoded natively in `mr_assets.c`; no screen capture
or DOS execution is involved. `3.NUM`/`3A.NUM` and `5.NUM`/`5A.NUM` are
23-entry MBF-single type-to-art maps. `4.NUM`/`4A.NUM` contain 32 fixed
125-word QuickBASIC `GET` slots (two sizes for each of fifteen art groups),
while `6.NUM`/`6A.NUM` contain 57 fixed 45-word slots (three distance sizes
for each of eighteen art groups). Each occupied slot starts with its width and
height words followed by byte-aligned, MSB-first scanlines. Rows consume exactly
`ceil(width_bits / 8)` bytes; unused fixed-slot storage follows the complete
bitmap and is not inserted between rows. The width word
is a bit count, so SCREEN 1 images decode at half that many pixels with four
packed two-bit CGA pixels per byte. The
paired shallow/deep files were saved from different DGROUP offsets, which the
loader validates individually rather than treating as interchangeable file
headers. A fifteenth deterministic suite checks the recovered maps, dimensions,
stride rules, and pixel data directly against the original resources.

The renderer no longer invents collision geometry from the appearance of a
DOSBox frame. `mr_render_trace_view` is a direct translation of DUNSMALL
`59DA-6144`: for each of the four cardinal passes it follows up to six cells,
retains the exact procedural 0..9 center/left/right wall values, records the
22-column occupancy slot at each depth, and terminates behind the first
blocking center edge. The native `mr_world_wall_value_for_step` is shared by
movement and projection so the two cannot silently disagree.

The top-down dungeon map is now a separate pure compositor translated from
`4CC3-53ED` and `485C-48F4`. It preserves the original x-major 20-by-19 cell
order, unrevealed-neighbor closure and revealed-neighbor seam ownership for
north/west edges, solid outside edges,
three-pixel horizontal and five-pixel vertical door gaps, circle/outline/
filled vertical-feature marks, hidden false floors, color-mode branches, and
the cropped SCREEN 1 CP437 direction arrows recovered at DS:`BA1E/BA22`.
The recovered initial `COLOR 0,2` pair now produces the original black field;
map walls are logical red, ladders and perspective walls logical green, and
the player/town/chute marks logical brown/yellow. The second compiled COLOR
submission repeats the same argument order after its BX/DX shuffle rather
than reversing it.
Deterministic raster tests cover doors, reveal seams, both ladder signs,
chutes, hidden pitfalls, boundaries, and a player arrow without running DOS.

The current four first-person wall views use a second pure compositor translated
from `62E2-699D`. It consumes the traced wall values and original F2 depth and
projection tables, preserves QB3 positive-half integer rounding, and emits the
recovered BRUN30 `LINE`, `LINE B`, `LINE BF`, and bounded `PAINT` operations in
source order. This includes solid terminating faces, passable center-door
slabs, perspective side-door polygons, level-zero ceiling suppression, color
and monochrome branches, and the six-level stopping rules. Fixed-pixel tests
cover a solid face, center door, painted side door, projection rounding, and
monochrome output. The original turn-in-place optimization is also fully
recovered: `580F-58F6` computes the cached cardinal-view rotation modulo four
and re-PUTs the four saved panes through the seven wraparound origins loaded
from F2.COM. The native renderer now retains the four original 53-pixel GET
buffers, including the extra side-pane scanline, and restores them through
the same `(previous-facing-current-facing) MOD 4` origin window. It also
preserves the source's XOR modes for the first/fourth buffers and PSET modes
for the middle two. Combat entry's second partial-redraw path is recovered too:
`78FF` refreshes the opposite/back pane once and then overlays the large
current-cell monster at absolute `(225,112)`. The native combat frame redraws
all panes before that same PUT. Incremental idle-monster redraws now compose
only the four changed pane rectangles into the host frame without XOR-erasing
the non-overlapping central combat sprite. Projected monster visibility now stops at
values 6 through 9, reads occupancy one cell beyond the current center edge,
and uses the five exact large/compact bitmap variants and local `PUT`
coordinates from `69BC-6B5A`. The asset decoder now interprets the GET header's
horizontal word as bits, yields the correct SCREEN 1 pixel widths, consumes
each scanline at its original byte-packed length without invented word
padding, decodes packed two-bit CGA colors, and applies default XOR PUT
composition. These component contracts are now original-derived, but the containing
  `593C` renderer and its cache/redraw call ordering remain unresolved in the
  routine gate; passing port-authored whole-frame hashes is not accepted as
  final raster certification.

The last provisionally named presentation helpers have now been resolved from
machine code as well. BEGIN, CHCHAR, DUNSMALL, and F8 use deterministic text
color counters where their earlier labels suggested randomness. F8's numeric
helper is the exact expression `RIGHT$(SPACE$(30)+STR$(value), width)`, and its
row colors repeat `10,11,12,13,14,9`. DUNSMALL's `C102` routine is the fixed
floor-zero `SBTIW` town-marker dispatcher, including all ten original town
coordinates, rather than a general graphics blitter. `A2E5` is the exact
eight-field `NAME` producer consumed by F8. These conclusions came from static
QB3/BRUN30 control-flow and data analysis; no DOSBox observation was used.

A subsequent whole-module data-xref pass corrected one nonvisual AI mistake.
DS:`B502/B506` are persistent monster-tracking slots owned by `70A1`, not the
first two sprites encountered by the renderer. DS:`B66A` is zeroed at the
start of every render and has no reachable nonzero writer. Therefore the
compiled `6CC5` path can clear B506 when it sees a monster other than B502,
but it never acquires B502 or B506 from viewport draw order. The native port
now keeps rendering and pursuit bookkeeping separate. The same pass proves
that deep resource type 16's DS:`B6F8` value is a 20-point turn-speed bonus:
it biases both post-hit initiative and the repeat-strike roll range.

## Confirmed game shape

- The Advanced game has 70 dungeon levels and a fountain-of-youth objective on
  level 70.
- The main play screen combines a top-down map with front, back, left, and
  right first-person views.
- Town contains three inns, a bank, temple, store, and wizard's guild.
- Dungeon traversal includes ropes, ladders, false floors/chutes, encumbrance,
  disease, poison, level drain, treasure conversion, and multi-view monsters.
- Character creation exposes Strength, Intelligence, Wisdom, Health, Agility,
  and Laziness, then race/class choices.
- Persisted attributes are exactly `INT(displayed_attribute * 3 + 237)` and
  decode as `(stored - 237) / 3`. The double-precision value 12,316 is the
  experience *storage offset*, not starting experience: a new character stores
  12,316 and loads as zero experience. The other scalar offsets follow the
  same deliberate save-obfuscation pattern described above.
- `tools/revenge/decompiled/CHCHAR.md` records the current address-backed
  high-level reconstruction, including all four race rows, reroll/accept/class
  validation, name normalization, the derived-stat formulas, and initial save
  state.
- `tools/revenge/decompiled/BEGIN.md` records the corrected `bz` branch-table
  layout, first-run color sentinel and timed introduction, exact 40-column
  menu, blinking 80-column roster selection, five menu arms, and module
  handoff files. `tools/revenge/decompiled/F8.md` records the exact
  eight-field death summary, ten-row hall-of-fame BSAVE update, sorting, and
  return path.
- The original tables contain at least 44 named monster records across shallow
  and deep groups. Each active set has 20 base types plus two high-health
  variants. Type is `(global_slot MOD 20)+1`; slots divisible by 2, 4, 8, and
  16 gain cumulative monster levels. The native formula module now preserves
  these non-random families rather than deduplicating repeated names.
- Six spell levels, preparation and battle casting, scroll-like magic items,
  colored pills, nine wand colors, equipment, treasure, inns, temple services,
  banking, and death/raise/reincarnation paths are all required.

## Native coverage by statically recovered code region

This table is the implementation inventory for the named procedures in
`revenge_source_map.json`. A checked row means the behavior was translated
from decoded code/data and is protected by the named native test; it does not
mean that a DOSBox frame was copied or visually approximated.

| Original region | Recovered responsibility | Native implementation / test |
| --- | --- | --- |
| `BEGIN 0040-0A67` | first-run color setup, two-page introduction, five-arm menu, roster and module handoffs | `mr_frontend.c`, `mr_game.c` / frontend, game |
| `CHCHAR 0030-285E` | explanations, race roll, class formulas, name/slot writes | `mr_character.c`, `mr_game.c` / character, game |
| `F8 0030-0635` | death summary, ten-row hall update/sort/return | `mr_frontend.c`, `mr_game.c` / frontend, game |
| `NCD 02AE-07E8` | BEGIN-reachable order page, P/p dispatch, printer records | `mr_game.c` / game |
| `DUNSMALL 0030-10FC`, `2F1A-3028`, `BF60-BFB9` | startup, input, sound/music, timing, movement modes | `mr_input.c`, `mr_game.c`, `mw_audio.c` / input, game |
| `DUNSMALL 10FD-2EF2` | rope, inventory, treasure drop, statistics and every town service | `mr_items.c`, `mr_town.c`, `mr_game.c` / items, town, game |
| `DUNSMALL 30C7-3DAE` | cardinal movement, chutes, preparation spells/items, fountain prompt | `mr_world.c`, `mr_spells.c`, `mr_session.c`, `mr_game.c` / world, spells, session |
| `DUNSMALL 3F5A-6DE7`, `C102-C331` | reveal/status map, four views, doors, sprites, town map marks | `mr_render.c`, `mr_assets.c`, `mr_game.c` / render, assets, game |
| `DUNSMALL 5417-5837` | explored rows, procedural walls, vertical-feature hash/ladders, cached-view rotation | `mr_world.c`, `mr_render.c` / world, render |
| `DUNSMALL 6EEE-7F7C`, `C7A5-C8AF` | monster art selection, occupancy, movement, resource-set switch | `mr_assets.c`, `mr_monsters.c`, `mr_session.c` / assets, monsters, session |
| `DUNSMALL 7F7D-A010` | active combat effects, combat loop, weapons, spells, items and monster turns | `mr_combat.c`, `mr_spells.c`, `mr_items.c`, `mr_session.c` / combat, spells, items, session |
| `DUNSMALL A013-A334` | death, reincarnation/raise, deletion and hall handoff | `mr_progression.c`, `mr_frontend.c`, `mr_game.c` / progression, frontend, game |
| `DUNSMALL A335-B2CE` | defeat, ordered loot pipeline, respawn and fountain placement | `mr_loot.c`, `mr_monsters.c`, `mr_session.c`, `mr_game.c` / loot, monsters, session, game |
| `DUNSMALL B308-B9B2` | exact character/global-monster persistence and resource reload | `mr_save.c`, `mr_world.c`, `mr_session.c` / save, world, session |
| `DUNSMALL BA10-C7A4` | reviews, character handoff, help chapters and shared selectors | `mr_data.c`, `mr_frontend.c`, `mr_game.c` / data, frontend, game |

The prior native fidelity audit claimed completion based on the following
native fixtures:

- full-frame deterministic hashes protect the solid-wall, center-door,
  side-door, map/player and rotated GET/PUT-cache rasters; the exact H1-H8
  help paging/repaint flow, `E` command overlay, and uncommon item/traversal
  dialogs are also recovered;
- NCD's direct-launch-only 80-column sales explanation is documented but is
  not exposed by BEGIN, whose option-five handoff provably jumps to `02AE`.

Those fixtures remain useful regression tests, but their hashes were generated
from the native translation and are therefore not an independent original
raster oracle.

## Superseded parity audit — 2026-08-30

This pass regenerated all five QB3 module graphs and the BRUN30 handler graph
from the supplied binaries, without launching DOSBox. The results reproduce
the checked-in recovery counts exactly: 865/2,455 reachable instructions/bytes
for BEGIN, 2,084/5,813 for CHCHAR, 18,306/51,292 for DUNSMALL, 519/1,404 for
F8, and 653/1,963 for NCD. Every direct procedure entry is present in the
high-confidence source map: 6/6 BEGIN, 6/6 CHCHAR, 126/126 DUNSMALL, 8/8 F8,
and 1/1 NCD. DUNSMALL's additional 17 and NCD's additional three statically
recovered GOSUB/helper entries are mapped as well.

The re-audit followed the complete BEGIN -> CHCHAR/DUNSMALL/F8/NCD control
surface and rechecked setup persistence, roster handoffs, all character save
forms, input and timing boundaries, dungeon/session transitions, monster and
combat state, ordered rewards, death return paths, help/town overlays, and the
SCREEN 0/SCREEN 1 presentation state. It found and corrected two assumptions
that had survived narrower fixtures:

- DUNSMALL's initial SCREEN 1 `COLOR 0,2` state uses a black effective
  background. Map walls are logical red; perspective walls and ladders are
  logical green. The old native order incorrectly promoted the palette
  selector to the background, producing the solid green field seen in the
  earlier map capture.
- DUNSMALL `B964-B97B` BLOADs character maps at an explicit DS:`9B06`
  destination. The BSAVE header address is ignored by the original runtime;
  valid supplied slots use `9B06`, `4020`, and `4050`. The native loader now
  accepts all of them, validates the actual 6,045-byte payload, preserves the
  original header words, and byte-round-trips every supplied `1.BIN` through
  `5.BIN`. A synthetic non-`9B06` regression fixture prevents recurrence.

All seventeen native Revenge test domains passed after those fixes. That fact
proved that the tests ran and that the implemented modules agreed with their
own expected values; it did **not** prove the formerly stated conclusions that
all reachable branches or pixels matched the original. The 2026-08-31
clean-room pass found defects despite the same green suite, so the old parity
conclusion is withdrawn. The intentional host adaptations remain explicit:
BEGIN option four returns to Moraff's World instead of DOS, printer output is
written to a local text spool, imported Revenge saves are isolated from the
original files, and modal input retains the recovered one-key versus repeating
boundaries.

## Clean-room parity re-audit — 2026-08-31

The authoritative inputs are freshly generated static graphs for all five QB3
modules and BRUN30, the original resource bytes, and independently transcribed
branch/RNG/raster contracts. Native module existence, address comments, and
self-generated frame hashes are classified as structural evidence only.

Confirmed defects found in the new pass:

- SCREEN 1 GET rows are byte-aligned packed two-bit scanlines. The stale asset
  schema and old decoder word-aligned each row, corrupting monster art.
- Combat S/M/K dispatch must reject unowned weapons, while F is unconditional
  fists. The old bridge made the ownership path inconsistent.
- DUNSMALL `0514` and `BA15` each call `B9B3` before the separate `BA1E`
  review-selection roll. Omitting those two color rolls shifted the complete
  later gameplay RNG stream.
- DUNSMALL's startup color table is 9..15 in color mode and all 15 in
  monochrome. The old help/review implementation had those modes reversed.
- F8 compares, thresholds, and sorts complete fixed 80-byte hall rows; parsing
  only the leading level changed equal-level ordering. Its BLOAD also has an
  explicit destination, so the BSAVE header address is informational.
- DUNSMALL `A886-A88D` tests integer treasure value before displaying coins;
  a positive low-copper count with value zero must skip the take/leave page.
- Successful raise at `A148-A159` subtracts one Health and immediately calls
  `2F3C`; the old native path recomputed its Health-derived factor before that
  acknowledgement could clamp a minimum-Health character back to one.

Still required before parity can be certified:

- independent original-derived raster expectations for QuickBASIC LINE,
  CIRCLE, PAINT, GET/PUT, font, map, door, and four-pane composition paths;
- exhaustive procedural-world comparison over every reachable seed, level,
  and coordinate tuple. BRUN30 SIN and exponentiation are recovered, but
  sampled fixtures do not make their enclosing world/render controllers
  exhaustive;
- branch-level evidence for every reachable selector, modal wait, RNG call,
  persistence boundary, and failure path;
- explicit classification and testing of every deliberate host adaptation.

## Legacy claimed completion gates (invalidated as certification)

The checkmarks below describe what the superseded audit claimed to cover. They
remain a work inventory; none is by itself a current 1:1 certification.

- [x] Reconstruct every gameplay-reachable character record and per-character
  binary field; classify unused array indices and preserve all reserved and
  padding bytes verbatim.
- [x] Recover every character-creation branch, validation rule, and timing
  (the apparent disk-space warnings are the statically proven 9/10-name
  roster-capacity checks, now reproduced at their original input boundaries).
- [x] Recover title/menu, hall of fame, and return/switch control flow.
- [x] Reconstruct dungeon generation, persisted monster/special-cell tables,
  fountain reseeding, and all 70 level transitions.
- [x] Match map reveal rules and all four first-person views (map and static
  wall/door raster plus monster selection/placement/composition recovered;
  exact fixed SCREEN 1 labels and combat status coordinates are ported;
  BRUN30's SCREEN 1 COLOR-register behavior and the `#`/`@` hardware-before-
  stored-wrap quirk are ported; original incremental background-buffer
  capture/rotation and its mixed XOR/PSET restoration are ported and covered
  by a full-frame deterministic fixture).
- [x] Match every town service, price, rejection, input boundary, and side
  effect recovered at `1E0A-2EF2`.
- [x] Match movement modes, DOS-key semantics, typematic/input polling,
  configured and calibrated delays, ladders, ropes, chutes, and weight.
- [x] Preserve automatic positive-floor chute traversal while covering it
  with a real `7.NUM` headless traversal fixture. A second fixture walks onto
  a real floor-zero filled-square down ladder, verifies that it does not
  auto-fall, then presses `D` and reaches the paired dungeon floor.
- [x] Match the statically recovered movement collision notices/order
  (correcting the superseded checklist's reversed interpretation):
  exploration movement may enter an occupied destination and starts combat
  from the player-cell check; combat-depth movement into another occupied
  destination prints `MONSTER BLOCKS WAY`; walls/bounds are silent,
  and the chute notice is printed before its pre-increment save with no
  invented wait.
- [x] Match both monster resource sets, persistent tables, spawning,
  cursor-driven movement, art selection, combat, and drains.
- [x] Match preparation and battle spells, magic items, all nine wands, all
  six pills, equipment, treasure conversion, and ordered defeat rewards.
- [x] Match death, raise, reincarnation, fountain, and hall-of-fame outcomes.
- [x] Implement original save import and isolated native Revenge saves.
- [x] Add a visible switch from Moraff's World's main menu and a return path.
- [x] Verify every recovered native display path against the disassembly as a
  final validation step (the invented level/coordinate/HP/SP HUD and command
  legend have been removed; BRUN30 palette-register semantics are ported; the
  preparation/battle spell, item, wand, and pill selector rows, exact strings,
  unavailable-slot forms, and original item-menu return rules are now
  statically matched; the
  sequential player-statistics report, both magic-inventory pages, shared
  row-25 wait prompt, inn PRINT wrapping/output order, and the store's omitted
  inventory-only statistics tail are now matched; the Wizard's Guild now
  reuses the original owned-item information selector and appends its source
  record descriptions; the bank now preserves its one-time introduction and
  redraws only the compact balance page after transfers; and the temple's
  introduction, menu order, result pages, and insufficient-funds path are now
  matched; the H1-H8 SCREEN 0 reader, record-25 pause rule, command-initial
  repaint and final-page prompt are matched; and the last traversal,
  preparation-item, combat-item, wand and pill result paths have been checked
  against their exact silent/statistics/timed branches).
- [x] Verify release packaging contains no original copyrighted binaries/data
  (a clean package build produced thirteen port/documentation files and its
  archive inspection found zero World or Revenge originals).
