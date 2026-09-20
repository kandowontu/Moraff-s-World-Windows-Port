# Bundled Moraff's Revenge runtime data

Moraff's Revenge is a native, statically reconstructed bonus game included in
this port. Version 1.1.3 and later bundle the complete runtime-data subset used
by the native reconstruction. Users do not need to install Revenge separately
or copy any Revenge files.

The package places these 36 seed resources in the local `revenge` directory:

```text
NAME
F1.COM  F2.COM  F5.COM  F6.COM  F7.COM  F9.EXE
1.EXE   1.BIN   2.EXE   2.BIN
1.NUM   2.NUM   3.NUM   4.NUM   5.NUM   6.NUM   7.NUM
3A.NUM  4A.NUM  5A.NUM  6A.NUM
H1.OVL  H2.OVL  H3.OVL  H4.OVL
H5.OVL  H6.OVL  H7.OVL  H8.OVL
REVIEW.1 REVIEW.2 REVIEW.3 REVIEW.4 REVIEW.5 REVIEW.6
```

On first use, the port seeds mutable character and world state into the sibling
`revenge-native` directory. It never alters the bundled resources. The two
numbered pairs correspond to the two reference characters named by the
preserved `F5.COM` roster; users can delete them or create new characters from
the native menu.

The old DOS program executables (`BEGIN.EXE`, `CHCHAR.EXE`, `DUNSMALL.EXE`,
`F8.EXE`, `NCD.EXE`, and `BRUN30.EXE`) are not executed or required at
runtime. They were statically disassembled to reconstruct the native code.

The port validates every required file's internal record or BSAVE layout when
it is loaded. The tested Moraff's Revenge 3.3 reference set has these sizes:

| Files | Sizes in bytes |
|---|---|
| `NAME`, `F1.COM`, `F2.COM`, `F5.COM`, `F6.COM`, `F7.COM`, `F9.EXE` | 4, 2043, 1744, 36, 272, 268, 1789 |
| `1.EXE`, `1.BIN`, `2.EXE`, `2.BIN` | 1026, 6053, 1024, 6053 |
| `1.NUM` through `7.NUM` | 5609, 5609, 100, 8007, 100, 5137, 6053 |
| `3A.NUM` through `6A.NUM` | 100, 8007, 100, 5137 |
| `H1.OVL` through `H8.OVL` | 1163, 2527, 1758, 1543, 1748, 2417, 1961, 1009 |
| `REVIEW.1` through `REVIEW.6` | 122 bytes each |

These bundled files remain copyrighted original-game material belonging to
Steve Moraff / MoraffWare and are not covered by the port's source-code
license. This document keeps its historical filename so older links continue
to work; it is now an inventory and ownership notice, not an installation
requirement.
