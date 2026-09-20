#!/usr/bin/env python3
"""Inventory locally owned Moraff's Revenge files without modifying them."""

from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
import math
import re
import struct
import sys
import zlib
from pathlib import Path


MZ_NAMES = {"BEGIN.EXE", "CHCHAR.EXE", "DUNSMALL.EXE", "F8.EXE", "NCD.EXE"}
SAVE_RE = re.compile(r"^[1-5]\.EXE$", re.IGNORECASE)


def file_hash(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def printable_strings(data: bytes, minimum: int = 4) -> list[dict[str, object]]:
    found: list[dict[str, object]] = []
    start = None
    for index, value in enumerate(data + b"\x00"):
        if 32 <= value <= 126 or value == 9:
            if start is None:
                start = index
        elif start is not None:
            if index - start >= minimum:
                found.append({"offset": start, "text": data[start:index].decode("cp437")})
            start = None
    return found


def mz_info(data: bytes) -> dict[str, object] | None:
    if len(data) < 28 or data[:2] != b"MZ":
        return None
    words = struct.unpack_from("<14H", data, 0)
    names = (
        "magic", "last_page_bytes", "pages", "relocations", "header_paragraphs",
        "minimum_extra_paragraphs", "maximum_extra_paragraphs", "initial_ss",
        "initial_sp", "checksum", "initial_ip", "initial_cs", "relocation_offset",
        "overlay",
    )
    values = dict(zip(names, words))
    image_size = values["pages"] * 512
    if values["last_page_bytes"]:
        image_size -= 512 - values["last_page_bytes"]
    values.update(
        header_bytes=values["header_paragraphs"] * 16,
        declared_image_bytes=image_size,
        entry_file_offset=values["header_paragraphs"] * 16
        + values["initial_cs"] * 16 + values["initial_ip"],
    )
    del values["magic"]
    return values


def bsave_info(data: bytes) -> dict[str, object] | None:
    # QuickBASIC BLOAD/BSAVE: FD, segment, offset, payload length, payload,
    # followed by the DOS text EOF marker in this title's files.
    if len(data) < 8 or data[0] != 0xFD:
        return None
    segment, offset, length = struct.unpack_from("<HHH", data, 1)
    if 7 + length > len(data):
        return None
    payload = data[7 : 7 + length]
    factors = []
    for divisor in range(1, math.isqrt(length) + 1):
        if length % divisor == 0:
            factors.append([divisor, length // divisor])
    return {
        "segment": segment,
        "offset": offset,
        "payload_length": length,
        "payload_sha256": hashlib.sha256(payload).hexdigest(),
        "trailing_bytes_hex": data[7 + length :].hex(),
        "factor_pairs": factors[-8:],
    }


def parse_save_records(data: bytes) -> dict[str, object] | None:
    try:
        text = data.rstrip(b"\x1a").decode("ascii")
    except UnicodeDecodeError:
        return None
    rows = list(csv.reader(io.StringIO(text)))
    parsed: list[list[object]] = []
    for row in rows:
        converted: list[object] = []
        for value in row:
            value = value.strip()
            try:
                converted.append(int(value))
            except ValueError:
                try:
                    converted.append(float(value))
                except ValueError:
                    converted.append(value)
        parsed.append(converted)
    return {"record_count": len(rows), "records": parsed}


def summarize(directory: Path, include_strings: bool) -> dict[str, object]:
    files: list[dict[str, object]] = []
    save_records: dict[str, list[list[object]]] = {}
    for path in sorted((item for item in directory.iterdir() if item.is_file()), key=lambda p: p.name.upper()):
        data = path.read_bytes()
        item: dict[str, object] = {
            "name": path.name,
            "length": len(data),
            "sha256": file_hash(path),
            "crc32": f"{zlib.crc32(data) & 0xFFFFFFFF:08x}",
        }
        mz = mz_info(data)
        if mz:
            item["mz"] = mz
            strings = printable_strings(data)
            item["printable_string_count"] = len(strings)
            item["quickbasic_brun30"] = any("BRUN30" in str(entry["text"]).upper() for entry in strings)
            if include_strings:
                item["strings"] = strings
        bsave = bsave_info(data)
        if bsave:
            item["quickbasic_bsave"] = bsave
        if SAVE_RE.match(path.name):
            parsed = parse_save_records(data)
            if parsed:
                item["save_record_count"] = parsed["record_count"]
                save_records[path.name] = parsed["records"]  # type: ignore[assignment]
        files.append(item)

    schema: list[dict[str, object]] = []
    if save_records:
        maximum = max(len(records) for records in save_records.values())
        for index in range(maximum):
            values = {
                name: records[index] if index < len(records) else None
                for name, records in save_records.items()
            }
            serialized = {json.dumps(value, sort_keys=True) for value in values.values()}
            schema.append({
                "record": index + 1,
                "field_count": max((len(value) for value in values.values() if isinstance(value, list)), default=0),
                "varies_between_slots": len(serialized) > 1,
                "values": values if index < 12 else None,
            })

    return {
        "format": "Moraff's Revenge local-source inventory v1",
        "directory": str(directory.resolve()),
        "file_count": len(files),
        "total_bytes": sum(int(item["length"]) for item in files),
        "expected_main_programs_present": sorted(MZ_NAMES.intersection({p.name.upper() for p in directory.iterdir()})),
        "files": files,
        "save_record_schema": schema,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    parser.add_argument("--json", type=Path, dest="json_path")
    parser.add_argument("--include-strings", action="store_true")
    args = parser.parse_args()
    if not args.directory.is_dir():
        parser.error(f"not a directory: {args.directory}")
    report = summarize(args.directory, args.include_strings)
    encoded = json.dumps(report, indent=2, ensure_ascii=False)
    if args.json_path:
        args.json_path.write_text(encoded + "\n", encoding="utf-8")
        print(args.json_path)
    else:
        print(encoded)
    return 0


if __name__ == "__main__":
    sys.exit(main())
