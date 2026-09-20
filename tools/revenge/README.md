# Moraff's Revenge analysis tools

These tools make the reverse-engineering work reproducible without committing
or redistributing MoraffWare's original files.

1. Extract a locally owned VHD:

   ```powershell
   .\extract_vhd.ps1 -VhdPath 'D:\Games\Moraffs Revenge.vhd' `
       -OutputDirectory "$env:TEMP\moraffs-revenge"
   ```

2. Inventory the extracted `Revenge` directory:

   ```powershell
   python .\analyze_revenge.py "$env:TEMP\moraffs-revenge\Revenge" `
       --json "$env:TEMP\moraffs-revenge-report.json"
   ```

3. Statically disassemble a QuickBASIC 3 module:

   ```powershell
   python .\disassemble_qb3.py DUNSMALL.EXE `
       --runtime BRUN30.EXE `
       --asm DUNSMALL.qb3.asm `
       --json DUNSMALL.qb3.json `
       --instructions-json DUNSMALL.qb3.instructions.json
   ```

   This decodes the selector byte consumed after `INT 3D`, `INT 3E`, and
   `INT 3F`, walks direct control flow, parses MZ relocations, and resolves
   selectors through the matching `BRUN30.EXE` dispatch tables. It does not
   execute the game or runtime. The Python `capstone` package is required.

   To reproduce the complete five-module analysis in one command, without
   launching DOSBox or executing any original program, run:

   ```powershell
   .\build_static_disassembly.ps1 `
       -RevengeDirectory 'D:\Games\Revenge' `
       -OutputDirectory "$env:TEMP\revenge-disassembly"
   ```

   If a separately owned QuickBASIC 3 compiler installation is available,
   pass its `BCOM30.LIB` with `-Bcom30Library`. The script will then recover
   the original Microsoft runtime public symbols before annotating all five
   game listings. A previously generated `BRUN30.symbols.json` in the output
   directory is also reused. The complete pipeline is static.

   The per-instruction JSON is a lossless machine-readable form of the
   recovered control-flow graph. `build_static_disassembly.ps1` also feeds it
   to `extract_qb3_graphics.py`, producing `*.qb3.graphics.json`. That IR
   groups the original QB3 `LINE`, `LINE -`, `CIRCLE`, `PAINT`, `GET`, `PUT`,
   and `VIEW` handler sequences and retains their exact coordinate-setup
   instruction slices. Native rendering can therefore be translated from the
   executable operations instead of reconstructed from screenshots.

   Recover the matching runtime-handler control flow with:

   ```powershell
   python .\disassemble_brun30.py BRUN30.EXE `
       --program DUNSMALL.EXE `
       --asm BRUN30.handlers.asm `
       --json BRUN30.handlers.json
   ```

   The BRUN30 listing begins at the runtime's MZ `CS:IP` entry point as well
   as every selector-table handler used by the five game modules. This keeps
   runtime initialization and handler recovery in the same static graph.

   BRUN30's 70 code relocations use segment `0000`; its sole nonzero-segment
   relocation uses `0FE0`, identifying the initialized DGROUP image at
   physical offset `0FE00h`. The runtime pass therefore reads the two
   seven-word PRINT vector sources at `DS:03A2` and `DS:03B0` from physical
   `101A2h` and `101B0h`. Routines `9B2D` and `9B84` copy them to
   `DS:0EBA-0EC6`; following those statically recovered pointers closes the
   character-output path without running BRUN30. The active printable path is
   `5F0E -> 272A -> 20D1`: `272A` selects BIOS `INT 10h/AH=09h`, a count of
   one, the current video page, and the current color. This proves that normal
   game text uses the BIOS 8x8 character generator rather than an embedded
   Moraff font.

   If the matching QuickBASIC 3 `BCOM30.LIB` is available from a separately
   owned compiler installation, recover Microsoft's original public runtime
   names and feed them into both listings:

   ```powershell
   python .\map_brun30_symbols.py BCOM30.LIB BRUN30.EXE `
       --json BRUN30.symbols.json
   python .\disassemble_qb3.py DUNSMALL.EXE `
       --runtime BRUN30.EXE `
       --runtime-symbols BRUN30.symbols.json `
       --data DUNSMALL.data.json `
       --source-map .\revenge_source_map.json `
       --asm DUNSMALL.qb3.asm `
       --json DUNSMALL.qb3.json
   python .\disassemble_brun30.py BRUN30.EXE `
       --symbols BRUN30.symbols.json `
       --program DUNSMALL.EXE `
       --asm BRUN30.handlers.asm `
       --json BRUN30.handlers.json
   ```

   `map_brun30_symbols.py` parses the Microsoft OMF library directly and
   matches its initialized code fragments against the runtime image. It does
   not invoke the compiler, linker, DOS, or BRUN30. The library and generated
   listings are local analysis inputs and are not distributed with the port.

   Recover the module's initialized DGROUP image and text destinations with:

   ```powershell
   python .\extract_qb3_data.py DUNSMALL.EXE `
       --json DUNSMALL.data.json
   ```

   For a compact `bm` module, the extractor follows the entry stub's second
   far call to the one-instruction initializer, takes the source image from
   the paragraph following its `RETF`, and reads the destination bounds from
   the module header. The report includes those exact file and DGROUP ranges,
   their byte count and SHA-256, plus each four-byte BASIC string descriptor
   and printable payload found inside the image. For example,
   `DUNSMALL.EXE` maps file offsets `C9F0`-`EB72` directly to
   DS:`B7D0`-`D952` (8,578 bytes). This is a structural reconstruction, not a
   scan heuristic.

   Four- and eight-byte numeric payloads are decoded as QuickBASIC's native
   Microsoft Binary Format single and double values. When the report is fed
   back to `disassemble_qb3.py`, referenced MBF singles at their exact DS
   addresses are annotated in the assembly listing; bytes occupied by string
   descriptors and payloads are excluded from numeric interpretation.
   Pass that report back to `disassemble_qb3.py` with
   `--data DUNSMALL.data.json` to annotate matching data references in the
   assembly listing.

   Recover compiled numeric `READ`/`DATA` statements separately with:

   ```powershell
   python .\extract_qb3_read_data.py CHCHAR.EXE `
       --json CHCHAR.read-data.json
   ```

   This statically parses the original BASIC line-numbered, NUL-terminated
   DATA streams. For example, it recovers the complete four-race by six-stat
   character-generation table from `CHCHAR.EXE`; it does not launch DOSBox or
   execute the original program.

   `revenge_source_map.json` contains the progressively verified names for
   recovered game procedures and persistent data fields. Each entry carries a
   confidence level so a structural or formula-level inference is not confused
   with an exact name preserved by Microsoft or the game data.

4. DOSBox-X is not part of source recovery. After the static reconstruction
   and native implementation are complete, `drive_dosbox.ps1` may be used as
   a final behavioral verification oracle against a locally owned copy.

5. Parse an original character save using the statically recovered record
   schema:

   ```powershell
   python .\parse_revenge_save.py .\1.EXE --binary .\1.BIN `
       --json .\1.save.json
   ```

   `revenge_save_schema.json` records the exact 311-record stream written at
   `DUNSMALL:B308` and read at `DUNSMALL:B674`, including QuickBASIC numeric
   types and array boundaries. The parser also decodes the `.BIN` sidecar's
   proven 71 by 21 explored-cell masks and preserves its 81-byte zero-filled
   fixed-endpoint padding.
   Both the literal stored values and decoded gameplay values are reported;
   this includes all eight stat/wealth offsets recovered from the loader.

6. Native development and release builds copy the bundled runtime subset from
   the source tree's relative `port/revenge` directory into the executable's
   relative `revenge` data directory:

   ```powershell
   cmake -S . -B build
   cmake --build build
   ```

   Runtime code opens `revenge/F1.COM`, `revenge/F2.COM`, and the other local
   bundled resources.
   On first entry to the native Revenge menu it seeds mutable state into the
   relative peer directory `revenge-native`. `NAME`, character pairs,
   `F5.COM`, `F9.EXE`, and the global monster tables are subsequently written
   only in that native directory; the bundled seed resources remain unchanged.
   The first numeric `NAME` record retains BEGIN's original `10` first-run
   sentinel and later `0`/`1` monochrome/color state. Each seed file is copied
   through a temporary file and `.seed-complete` is created only after the
   whole initial import succeeds, so an interrupted import is resumed instead
   of being accepted as complete.

Selector names are deliberately conservative. QuickBASIC 4/QBasic selector
tables are incompatible with QuickBASIC 3, so only meanings proven from QB3
control flow are labeled.

The expanded QB3 `bz` module layout used by `BEGIN.EXE`, `CHCHAR.EXE`, and
`NCD.EXE` stores inline `GOSUB` and computed `ON GOTO`/`ON GOSUB` code-label
words relative to the payload at image offset `10h`.  The disassembler applies
that format-specific `+10h` target adjustment to both forms. BEGIN's five-way
dispatch and NCD's three clean GOSUB entry points prove the rule independently.
Compact `bm` modules such as `DUNSMALL.EXE` and `F8.EXE` store physical image
offsets in their code-label words and receive no adjustment. Their initialized
data is the separate contiguous DGROUP image described above.

Direct `CALL FAR` (`9Ah`) operands are decoded as their original
segment:offset pair and also reported as a physical module-image offset. They
are deliberately excluded from the near-procedure inventory: in
`DUNSMALL.EXE`, for example, the entry point calls compiler startup/loader
glue beyond the ordinary near-code segment. This prevents those targets from
being mistaken for BASIC gameplay procedures or initialized data.

The scripts take all paths as parameters. Nothing in the native port depends
on the developer's network share or another absolute machine-specific path.
