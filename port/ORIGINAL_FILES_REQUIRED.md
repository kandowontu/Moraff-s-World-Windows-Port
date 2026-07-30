# Original Moraff's World files required

Place all ten files directly beside `moraffs_world.exe`.

These files are copyrighted original-game material belonging to Steve Moraff /
MoraffWare. They are not part of this native-port release and are not covered
by its MIT license. Copy them from your own legally obtained Moraff's World
installation.

## Approved executable variants

| File | Exact tested size | CRC-32 | SHA-256 |
|---|---:|---|---|
| `MW.EXE` | 12,823 bytes | `30C074B7` | `6EA1A430AE34185399CF3C19ACFADDEC8DC52A20D59EED9172F266C5EF7858B9` |
| `WORLD.EXE` | 229,480 bytes | `9ABA4217` | `04ADD8AA22947896A5A53D7698DB92CE33F4C9AFEE9D23831B37E40416092365` |
| `WORLD.EXE` | 104,316 bytes | `2FDC68F1` | `DC5DC918028AA36FFA63723BD149CF3BAC89CC705C31E3F44A779C13FBC7CA80` |

## Original data reference set

| File | Exact tested size | SHA-256 |
|---|---:|---|
| `DUNG.BIN` | 12,800 bytes | `21BD042DC85053D9C75A72AF809A63CE0C71A5A432CB203D6DC44F67BDF06793` |
| `WORLDMAP.BIN` | 4,096 bytes | `30365804B518F8520948418B869DF28E815573B663A723F38A640F9261B9B52C` |
| `H.BIN` | 6,162 bytes | `E5ACA256884211D2143FABC84A677C2565BFE6164E59C91345BC2BCBB7936F34` |
| `WORLD.PIC` | 175,063 bytes | `4CE5BBDE29A37D8B846973B26D112F7F9D192C8B733CEF99641D210AAD47C72C` |
| `WALL.PIC` | 11,984 bytes | `D0991BD9E521D49629DACEE66D0B63D3AF570C0BB5D7127B3F79BD9BDD79678D` |
| `360X480.FNT` | 6,164 bytes | `1D14035EAA8894397D2E75571E8BD3F449B6FFBEEF04D4268961A5B3C219976A` |
| `320X200.FNT` | 2,576 bytes | `6530EB96C6F59832A4F69277F2AA28284F00DB2BE566A871AEC2E35F80A3B468` |
| `ROLL.TXT` | 2,432 bytes | `8B6E3FA3BDE9B97240D71F477B26D6EA3D098A72134AEBCC2B4E5F20188A87EF` |

## What the program enforces

The program requires all ten filenames to exist. It additionally enforces an
approved combination of exact size, CRC-32, and SHA-256 for `MW.EXE` and
`WORLD.EXE`. Hashes for the eight data files document the asset set used to
develop and test this release.

Either listed `WORLD.EXE` variant is accepted. Do not substitute a modified,
truncated, or unknown executable.

## Final directory example

```text
Moraffs World Native Port\
  moraffs_world.exe
  MW.EXE
  WORLD.EXE
  DUNG.BIN
  WORLDMAP.BIN
  H.BIN
  WORLD.PIC
  WALL.PIC
  360X480.FNT
  320X200.FNT
  ROLL.TXT
  README.md
  ORIGINAL_FILES_REQUIRED.md
  CREDITS.md
  THIRD_PARTY_NOTICES.md
  LICENSE_PORT.txt
```

The game creates save and dungeon-state files in this directory after play
begins.
