# Original Moraff's Revenge files required

Moraff's Revenge is a native, statically reconstructed bonus game in this
port. The release does not contain Steve Moraff's original Revenge data.

To enable it, create a directory named `revenge` beside
`moraffs_world.exe` and copy these files from a legally obtained Moraff's
Revenge 3.3 installation into it:

```text
NAME
F1.COM  F2.COM  F5.COM  F6.COM  F7.COM  F9.EXE
1.NUM   2.NUM   3.NUM   4.NUM   5.NUM   6.NUM   7.NUM
3A.NUM  4A.NUM  5A.NUM  6A.NUM
H1.OVL  H2.OVL  H3.OVL  H4.OVL
H5.OVL  H6.OVL  H7.OVL  H8.OVL
REVIEW.1 REVIEW.2 REVIEW.3 REVIEW.4 REVIEW.5 REVIEW.6
```

If `F5.COM` contains existing character names, also copy the matching
numbered save pairs (`1.EXE` + `1.BIN`, `2.EXE` + `2.BIN`, and so on).
The port imports those characters once into the sibling `revenge-native`
directory and does not alter the originals.

The old DOS program executables (`BEGIN.EXE`, `CHCHAR.EXE`, `DUNSMALL.EXE`,
`F8.EXE`, `NCD.EXE`, and `BRUN30.EXE`) are not executed or required at
runtime. They were statically disassembled to reconstruct the native code.

The port validates every required file's internal record or BSAVE layout when
it is loaded. The tested Moraff's Revenge 3.3 reference set has these sizes:

| Files | Sizes in bytes |
|---|---|
| `NAME`, `F1.COM`, `F2.COM`, `F5.COM`, `F6.COM`, `F7.COM`, `F9.EXE` | 4, 2043, 1744, 36, 272, 268, 1789 |
| `1.NUM` through `7.NUM` | 5609, 5609, 100, 8007, 100, 5137, 6053 |
| `3A.NUM` through `6A.NUM` | 100, 8007, 100, 5137 |
| `H1.OVL` through `H8.OVL` | 1163, 2527, 1758, 1543, 1748, 2417, 1961, 1009 |
| `REVIEW.1` through `REVIEW.6` | 122 bytes each |

These files remain copyrighted original-game material belonging to Steve
Moraff / MoraffWare and are not covered by the port's source-code license.
