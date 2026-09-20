#!/usr/bin/env python3
"""Parse and validate an original Moraff's Revenge character save.

The schema is recovered statically from DUNSMALL.EXE's B308 save procedure and
B674 load procedure.  The parser does not execute any DOS program.
"""

from __future__ import annotations

import argparse
import copy
import json
import struct
from pathlib import Path


def parse_number(raw: str) -> int | float:
    value = float(raw.strip())
    return int(value) if value.is_integer() else value


def parse_record(raw: str) -> list[int | float]:
    return [parse_number(part) for part in raw.split(",")]


def normalized_number(value: int | float) -> int | float:
    """Keep the JSON readable when an exact transform produces an integer."""
    numeric = float(value)
    rounded = round(numeric)
    return int(rounded) if abs(numeric - rounded) < 1e-9 else numeric


def decode_transform(value: int | float, transform: dict[str, object]) -> int | float:
    multiplier = float(transform.get("multiplier", 1))
    offset = float(transform.get("offset", 0))
    if multiplier == 0:
        raise ValueError("save transform multiplier cannot be zero")
    return normalized_number((float(value) - offset) / multiplier)


def decode_gameplay_values(
    stored: dict[str, object], schema: dict[str, object]
) -> dict[str, object]:
    """Apply the exact anti-tamper/storage transforms used by DUNSMALL."""
    decoded = copy.deepcopy(stored)
    for group in schema["records"]:
        name = str(group["name"])
        value = decoded[name]
        element_transform = group.get("element_transform")
        if element_transform:
            if not isinstance(value, dict):
                raise ValueError(f"{name}: element transform requires a named mapping")
            decoded[name] = {
                key: decode_transform(item, element_transform)
                for key, item in value.items()
            }
        field_transforms = group.get("field_transforms", {})
        if field_transforms:
            if not isinstance(value, dict):
                raise ValueError(f"{name}: field transforms require a named mapping")
            for field, transform in field_transforms.items():
                value[field] = decode_transform(value[field], transform)
    return decoded


def load_schema(path: Path) -> dict[str, object]:
    return json.loads(path.read_text(encoding="utf-8"))


def decode_mbf_single(raw: bytes) -> float:
    """Decode the four-byte Microsoft Binary Format used by QuickBASIC 3."""
    if len(raw) != 4:
        raise ValueError("an MBF single must contain four bytes")
    exponent = raw[3]
    if exponent == 0:
        return 0.0
    mantissa = raw[0] | (raw[1] << 8) | ((raw[2] & 0x7F) << 16)
    sign = -1.0 if raw[2] & 0x80 else 1.0
    return sign * (1.0 + mantissa / float(1 << 23)) * 2.0 ** (exponent - 129)


def parse_binary_sidecar(path: Path, schema: dict[str, object]) -> dict[str, object]:
    raw = path.read_bytes()
    if len(raw) < 8 or raw[0] != 0xFD:
        raise ValueError(f"{path.name}: not a QuickBASIC BSAVE file")
    segment, offset, payload_length = struct.unpack_from("<HHH", raw, 1)
    expected = int(schema["binary_sidecar"]["payload_bytes"])
    if payload_length != expected:
        raise ValueError(
            f"{path.name}: BSAVE header says {payload_length} payload bytes; "
            f"expected {expected}"
        )
    payload = raw[7 : 7 + payload_length]
    if len(payload) != payload_length:
        raise ValueError(f"{path.name}: truncated BSAVE payload")
    trailing_file_bytes = raw[7 + payload_length :]
    if trailing_file_bytes not in (b"", b"\x1a"):
        raise ValueError(f"{path.name}: unexpected bytes after BSAVE payload")

    grid = schema["binary_sidecar"]["grid"]
    levels = int(grid["levels"])
    rows_per_level = int(grid["rows_per_level"])
    mapped_count = levels * rows_per_level
    masks = []
    for index in range(mapped_count):
        value = decode_mbf_single(payload[index * 4 : index * 4 + 4])
        rounded = round(value)
        if abs(value - rounded) > 0.0001 or not 0 <= rounded < (1 << 21):
            raise ValueError(
                f"{path.name}: explored-cell element {index} is not a 21-bit mask"
            )
        masks.append(int(rounded))

    rows = [
        masks[level * rows_per_level : (level + 1) * rows_per_level]
        for level in range(levels)
    ]
    mapped_bytes = mapped_count * 4
    return {
        "source": str(path),
        "bsave_segment": segment,
        "bsave_offset": offset,
        "payload_length": payload_length,
        "row_masks_by_level": rows,
        "trailing_payload_hex": payload[mapped_bytes:].hex(),
        "dos_eof_present": trailing_file_bytes == b"\x1a",
    }


def parse_save(
    path: Path, schema: dict[str, object], binary_path: Path | None = None
) -> dict[str, object]:
    text = path.read_bytes().decode("cp437").rstrip("\x1a\r\n")
    lines = text.splitlines()
    expected_count = int(schema["evidence"]["record_count"])
    if len(lines) != expected_count:
        raise ValueError(
            f"{path.name}: expected {expected_count} records, found {len(lines)}"
        )

    parsed: dict[str, object] = {}
    for group in schema["records"]:
        first = int(group["first"])
        last = int(group["last"])
        records = [parse_record(line) for line in lines[first - 1 : last]]
        fields = group.get("fields")
        element_names = group.get("element_names")
        if fields:
            if len(records) == 1:
                if len(records[0]) != len(fields):
                    raise ValueError(
                        f"record {first} has {len(records[0])} values; "
                        f"expected {len(fields)}"
                    )
                value: object = dict(zip(fields, records[0]))
            else:
                for offset, record in enumerate(records):
                    if len(record) != len(fields):
                        raise ValueError(
                            f"record {first + offset} has {len(record)} values; "
                            f"expected {len(fields)}"
                        )
                value = [dict(zip(fields, record)) for record in records]
        elif element_names:
            if len(records) != len(element_names):
                raise ValueError(f"{group['name']}: invalid element-name count")
            if any(len(record) != 1 for record in records):
                raise ValueError(f"records {first}-{last} must contain one value each")
            value = {
                name: record[0] for name, record in zip(element_names, records)
            }
        else:
            if any(len(record) != 1 for record in records):
                raise ValueError(f"records {first}-{last} must contain one value each")
            value = [record[0] for record in records]
        parsed[str(group["name"])] = value

    result = {
        "format": str(schema["format"]),
        "source": str(path),
        "record_count": len(lines),
        "character": decode_gameplay_values(parsed, schema),
        "stored_character": parsed,
    }
    if binary_path is not None:
        result["binary_sidecar"] = parse_binary_sidecar(binary_path, schema)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("save", type=Path, help="original 1.EXE through 5.EXE save")
    parser.add_argument(
        "--schema",
        type=Path,
        default=Path(__file__).with_name("revenge_save_schema.json"),
    )
    parser.add_argument(
        "--binary",
        type=Path,
        help="paired original QuickBASIC BSAVE sidecar (for example 1.BIN)",
    )
    parser.add_argument("--json", type=Path, dest="json_path")
    args = parser.parse_args()

    report = parse_save(args.save, load_schema(args.schema), args.binary)
    output = json.dumps(report, indent=2, ensure_ascii=False) + "\n"
    if args.json_path:
        args.json_path.write_text(output, encoding="utf-8")
    else:
        print(output, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
