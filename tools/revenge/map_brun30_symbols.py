#!/usr/bin/env python3
"""Recover BRUN30 symbol names from a matching QuickBASIC 3 OMF library.

BCOM30.LIB contains the same Microsoft BASIC runtime implementation used to
build BRUN30.EXE, but retains OMF module and PUBDEF names. This tool parses the
library without invoking the DOS compiler or runtime, matches initialized code
fragments against the supplied BRUN30 image, and reports the resulting runtime
addresses. It does not copy either input into its output.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path

from disassemble_qb3 import read_mz


THEADR = 0x80
MODEND_TYPES = {0x8A, 0x8B}
PUBDEF_TYPES = {0x90, 0x91}
LNAMES = 0x96
SEGDEF_TYPES = {0x98, 0x99}
LEDATA_TYPES = {0xA0, 0xA1}
LIBHDR = 0xF0
LIBEND = 0xF1


@dataclass
class Segment:
    name: str
    class_name: str
    declared_length: int
    chunks: list[tuple[int, bytes]] = field(default_factory=list)


@dataclass
class Public:
    name: str
    segment_index: int
    offset: int


@dataclass
class Module:
    name: str
    source_offset: int
    names: list[str] = field(default_factory=lambda: [""])
    segments: list[Segment | None] = field(default_factory=lambda: [None])
    publics: list[Public] = field(default_factory=list)


def read_index(data: bytes, position: int) -> tuple[int, int]:
    if position >= len(data):
        raise ValueError("truncated OMF index")
    first = data[position]
    if first & 0x80:
        if position + 1 >= len(data):
            raise ValueError("truncated two-byte OMF index")
        return ((first & 0x7F) << 8) | data[position + 1], position + 2
    return first, position + 1


def read_name(data: bytes, position: int) -> tuple[str, int]:
    if position >= len(data):
        raise ValueError("truncated OMF name")
    length = data[position]
    end = position + 1 + length
    if end > len(data):
        raise ValueError("truncated OMF name payload")
    return data[position + 1 : end].decode("ascii", errors="replace"), end


def record_at(raw: bytes, position: int) -> tuple[int, bytes, int]:
    if position + 3 > len(raw):
        raise ValueError(f"truncated OMF record header at {position:#x}")
    record_type = raw[position]
    length = struct.unpack_from("<H", raw, position + 1)[0]
    end = position + 3 + length
    if not length or end > len(raw):
        raise ValueError(f"invalid OMF record length at {position:#x}")
    record = raw[position:end]
    if record_type in {LIBHDR, LIBEND}:
        # Library framing records use the declared bytes as metadata/padding;
        # unlike object records, the final byte is not an OMF checksum.
        return record_type, raw[position + 3 : end], end
    # Microsoft libraries in this distribution contain a handful of maximum
    # size LEDATA records whose stored checksum does not balance. Their record
    # boundaries and following FIXUPP records are nevertheless well formed,
    # so checksum validity is not used as a parsing boundary.
    return record_type, raw[position + 3 : end - 1], end


def parse_library(path: Path) -> list[Module]:
    raw = path.read_bytes()
    record_type, header, page_size = record_at(raw, 0)
    if record_type != LIBHDR:
        raise ValueError(f"{path} is not a Microsoft OMF library")
    if page_size & (page_size - 1):
        raise ValueError(f"unsupported OMF library page size {page_size}")

    # The first LIBHDR dword is the dictionary offset. It bounds object pages
    # even when a producer omits a conventional LIBEND record.
    dictionary_offset = struct.unpack_from("<I", header, 0)[0]
    limit = min(len(raw), dictionary_offset or len(raw))
    modules: list[Module] = []
    position = page_size
    while position < limit:
        if raw[position] == LIBEND:
            break
        if raw[position] != THEADR:
            position = (position + page_size) & ~(page_size - 1)
            continue
        module_start = position
        _, payload, position = record_at(raw, position)
        name, _ = read_name(payload, 0)
        module = Module(name=name, source_offset=module_start)

        while position < limit:
            record_type, payload, position = record_at(raw, position)
            if record_type == LNAMES:
                cursor = 0
                while cursor < len(payload):
                    value, cursor = read_name(payload, cursor)
                    module.names.append(value)
            elif record_type in SEGDEF_TYPES:
                cursor = 0
                attributes = payload[cursor]
                cursor += 1
                alignment = attributes >> 5
                if alignment == 0:
                    cursor += 3  # absolute frame and offset
                if record_type & 1:
                    if cursor + 4 > len(payload):
                        raise ValueError("truncated 32-bit SEGDEF")
                    declared_length = struct.unpack_from("<I", payload, cursor)[0]
                    cursor += 4
                else:
                    if cursor + 2 > len(payload):
                        raise ValueError("truncated 16-bit SEGDEF")
                    declared_length = struct.unpack_from("<H", payload, cursor)[0]
                    cursor += 2
                    if declared_length == 0 and attributes & 0x02:
                        declared_length = 0x10000
                name_index, cursor = read_index(payload, cursor)
                class_index, cursor = read_index(payload, cursor)
                _, cursor = read_index(payload, cursor)  # overlay name
                name_value = module.names[name_index] if name_index < len(module.names) else ""
                class_value = module.names[class_index] if class_index < len(module.names) else ""
                module.segments.append(Segment(name_value, class_value, declared_length))
            elif record_type in PUBDEF_TYPES:
                cursor = 0
                _, cursor = read_index(payload, cursor)  # base group
                segment_index, cursor = read_index(payload, cursor)
                if segment_index == 0:
                    cursor += 2  # explicit frame
                while cursor < len(payload):
                    name_value, cursor = read_name(payload, cursor)
                    width = 4 if record_type & 1 else 2
                    if cursor + width > len(payload):
                        raise ValueError("truncated PUBDEF offset")
                    if width == 4:
                        offset = struct.unpack_from("<I", payload, cursor)[0]
                    else:
                        offset = struct.unpack_from("<H", payload, cursor)[0]
                    cursor += width
                    _, cursor = read_index(payload, cursor)  # type index
                    module.publics.append(Public(name_value, segment_index, offset))
            elif record_type in LEDATA_TYPES:
                cursor = 0
                segment_index, cursor = read_index(payload, cursor)
                width = 4 if record_type & 1 else 2
                if cursor + width > len(payload):
                    raise ValueError("truncated LEDATA offset")
                if width == 4:
                    offset = struct.unpack_from("<I", payload, cursor)[0]
                else:
                    offset = struct.unpack_from("<H", payload, cursor)[0]
                cursor += width
                if segment_index >= len(module.segments) or module.segments[segment_index] is None:
                    raise ValueError("LEDATA references an unknown segment")
                module.segments[segment_index].chunks.append((offset, payload[cursor:]))
            if record_type in MODEND_TYPES:
                break

        modules.append(module)
        position = (position + page_size - 1) & ~(page_size - 1)
    return modules


def unique_find(haystack: bytes, needle: bytes) -> int | None:
    first = haystack.find(needle)
    if first < 0 or haystack.find(needle, first + 1) >= 0:
        return None
    return first


def match_segment(segment: Segment, runtime: bytes) -> tuple[int | None, int, int, int]:
    votes: Counter[int] = Counter()
    for chunk_offset, chunk in segment.chunks:
        for width in (48, 32, 24, 16):
            if len(chunk) < width:
                continue
            step = max(4, width // 4)
            for local in range(0, len(chunk) - width + 1, step):
                found = unique_find(runtime, chunk[local : local + width])
                if found is not None:
                    votes[found - chunk_offset - local] += width
            if votes:
                break
    if not votes:
        return None, 0, 0, 0
    base, vote_weight = votes.most_common(1)[0]
    matched = 0
    compared = 0
    for chunk_offset, chunk in segment.chunks:
        start = base + chunk_offset
        if start < 0 or start + len(chunk) > len(runtime):
            continue
        compared += len(chunk)
        matched += sum(a == b for a, b in zip(chunk, runtime[start : start + len(chunk)]))
    return base, vote_weight, matched, compared


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library", type=Path, help="matching QuickBASIC 3 BCOM30.LIB")
    parser.add_argument("runtime", type=Path, help="BRUN30.EXE to name")
    parser.add_argument("--json", type=Path, dest="json_path")
    args = parser.parse_args()

    modules = parse_library(args.library)
    _, _, runtime, _ = read_mz(args.runtime)
    mapped_segments = []
    symbols = []
    for module in modules:
        segment_bases: dict[int, int] = {}
        for index, segment in enumerate(module.segments):
            if index == 0 or segment is None or not segment.chunks:
                continue
            base, votes, matched, compared = match_segment(segment, runtime)
            confidence = matched / compared if compared else 0.0
            record = {
                "module": module.name,
                "module_source_offset": f"{module.source_offset:06X}",
                "segment_index": index,
                "segment_name": segment.name,
                "class_name": segment.class_name,
                "declared_length": segment.declared_length,
                "runtime_base": None if base is None else f"{base:04X}",
                "vote_weight": votes,
                "matching_bytes": matched,
                "compared_bytes": compared,
                "confidence": round(confidence, 6),
            }
            mapped_segments.append(record)
            if base is not None and confidence >= 0.65:
                segment_bases[index] = base
        for public in module.publics:
            base = segment_bases.get(public.segment_index)
            symbols.append({
                "name": public.name,
                "module": module.name,
                "segment_index": public.segment_index,
                "segment_offset": f"{public.offset:04X}",
                "runtime_address": None if base is None else f"{base + public.offset:04X}",
            })

    report = {
        "format": "QuickBASIC 3 BRUN symbol recovery v1",
        "library": str(args.library.resolve()),
        "runtime": str(args.runtime.resolve()),
        "module_count": len(modules),
        "mapped_segment_count": sum(item["runtime_base"] is not None for item in mapped_segments),
        "named_runtime_symbol_count": sum(item["runtime_address"] is not None for item in symbols),
        "segments": mapped_segments,
        "symbols": symbols,
    }
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
