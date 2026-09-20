#!/usr/bin/env python3
"""Recover QuickBASIC READ/DATA streams from a compiled DOS module.

QuickBASIC 3 leaves numeric DATA statements as NUL-terminated ASCII streams.
Each stream is preceded by its original BASIC line number and the sequence is
terminated by FFFFh.  This parser reports those values without executing the
program or BRUN30.  It deliberately accepts only numeric CSV streams; ordinary
game strings therefore cannot be mistaken for DATA statements.
"""

from __future__ import annotations

import argparse
import json
import re
import struct
import sys
from pathlib import Path


NUMERIC_STREAM = re.compile(
    rb"[ \t]*[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[EeDd][+-]?\d+)?"
    rb"(?:[ \t]*,[ \t]*[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[EeDd][+-]?\d+)?)+[ \t]*"
)


def parse_number(value: str) -> int | float:
    value = value.strip().replace("D", "E").replace("d", "e")
    if not any(character in value for character in ".Ee"):
        return int(value)
    return float(value)


def recover(path: Path) -> dict[str, object]:
    raw = path.read_bytes()
    statements: list[dict[str, object]] = []
    occupied_until = 0
    for match in NUMERIC_STREAM.finditer(raw):
        start, end = match.span()
        if start < occupied_until or start < 2 or end >= len(raw) or raw[end] != 0:
            continue
        line = struct.unpack_from("<H", raw, start - 2)[0]
        # Real BASIC source lines are positive and the two-byte FFFFh stream
        # terminator can never be a valid line number.
        if line in (0, 0xFFFF):
            continue
        text = match.group().decode("ascii").strip()
        values = [parse_number(value) for value in text.split(",")]
        statements.append({
            "file_offset": start - 2,
            "line_number": line,
            "text": text,
            "values": values,
        })
        occupied_until = end + 1
    return {
        "format": "QuickBASIC 3 compiled READ/DATA stream v1",
        "executable": str(path.resolve()),
        "statement_count": len(statements),
        "value_count": sum(len(item["values"]) for item in statements),
        "statements": statements,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--json", type=Path, dest="json_path")
    args = parser.parse_args()
    report = recover(args.executable)
    encoded = json.dumps(report, indent=2) + "\n"
    if args.json_path:
        args.json_path.parent.mkdir(parents=True, exist_ok=True)
        args.json_path.write_text(encoded, encoding="utf-8")
        print(args.json_path)
    else:
        print(encoded, end="")
    return 0


if __name__ == "__main__":
    sys.exit(main())
