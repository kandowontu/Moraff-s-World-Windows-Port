#!/usr/bin/env python3
"""Recover initialized-data records from QuickBASIC 3 DOS modules.

The large ``bm`` modules carry a direct contiguous DGROUP image; the smaller
``bz`` modules store BRUN's expanded initialization stream. Both carry final
data-segment destinations, so they can be reconstructed without running either
program.

This is a static parser. It does not execute the analyzed program or BRUN30.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from dataclasses import asdict, dataclass
from pathlib import Path

from disassemble_qb3 import MzHeader, auto_code_bounds, read_mz


@dataclass(frozen=True)
class DataRecord:
    source_offset: int
    length: int
    destination: int
    payload_hex: str
    text: str | None
    descriptor: int | None
    mbf_single: float | None
    mbf_double: float | None


@dataclass(frozen=True)
class DataRun:
    encoding: str
    source_start: int
    source_end: int
    terminator_offset: int
    records: tuple[DataRecord, ...]


def word(image: bytes, offset: int) -> int:
    return struct.unpack_from("<H", image, offset)[0]


def display_text(payload: bytes) -> str | None:
    if not payload:
        return None
    printable = sum(value in (9, 10, 13) or 32 <= value <= 126 for value in payload)
    if printable / len(payload) < 0.80:
        return None
    return payload.decode("cp437", errors="replace")


def decode_mbf(payload: bytes) -> float | None:
    """Decode a Microsoft Binary Format single or double value.

    QuickBASIC 3 stores the mantissa least-significant byte first, the sign in
    bit 7 of the final mantissa byte, and the biased exponent in the last byte.
    Zero is represented by an exponent of zero.  Keeping this here makes the
    reconstructed initialized-data reports self describing and avoids ever
    executing BRUN30 merely to inspect numeric constants.
    """

    if len(payload) not in (4, 8):
        return None
    exponent = payload[-1]
    if exponent == 0:
        return 0.0
    mantissa_bytes = bytearray(payload[:-1])
    negative = bool(mantissa_bytes[-1] & 0x80)
    mantissa_bytes[-1] &= 0x7F
    fraction_bits = 8 * len(mantissa_bytes) - 1
    fraction = int.from_bytes(mantissa_bytes, "little") / (1 << fraction_bits)
    value = (1.0 + fraction) * (2.0 ** (exponent - 129))
    return -value if negative else value


def numeric_views(payload: bytes, text_value: str | None) -> tuple[float | None, float | None]:
    if text_value is not None:
        return None, None
    if len(payload) == 4:
        return decode_mbf(payload), None
    if len(payload) == 8:
        return None, decode_mbf(payload)
    return None, None


def module_data_limits(image: bytes) -> tuple[int, int]:
    if image[:2] == b"bm":
        # Large modules copy a single contiguous initialized image into
        # DGROUP.  The module header carries that image's exact destination
        # bounds at 20h and 0Ch respectively.  Offset 0Eh is the BLOAD base,
        # not the initialized-data base.
        return word(image, 0x20), word(image, 0x0C)
    elif len(image) >= 18 and image[16:18] == b"bz":
        base = 0x10
    else:
        base = 0
    if len(image) < base + 0x12:
        return 0, 0x10000
    start = word(image, base + 0x0E)
    # Offset 0Ch is the high-water mark of BASIC's data segment. Some small
    # modules leave it below the initialized-data base.
    limit = word(image, base + 0x0C)
    if limit <= start:
        limit = 0x10000
    return start, limit


def initializer_source_limits(
    header: MzHeader,
    image: bytes,
    relocations: list[object],
) -> tuple[int, int]:
    if image[:2] == b"bm":
        # The second direct far call in a bm module's entry stub reaches the
        # module-specific initializer.  It consists solely of RETF; the
        # initialized DGROUP image begins at the following paragraph.  This
        # relationship is encoded by the executable itself and avoids a
        # heuristic scan through loader machine code.
        entry = header.initial_cs * 16 + header.initial_ip
        second_call = entry + 5
        if second_call + 5 > len(image) or image[second_call] != 0x9A:
            raise ValueError("bm module has no second initializer far call")
        offset, segment = struct.unpack_from("<HH", image, second_call + 1)
        initializer = segment * 16 + offset
        source_start = (initializer + 1 + 0x0F) & ~0x0F
        destination_start, destination_end = module_data_limits(image)
        source_end = source_start + destination_end - destination_start
        if not (
            0 <= destination_start <= destination_end <= 0x10000
            and 0 <= source_start <= source_end <= len(image)
        ):
            raise ValueError("bm initialized-data image lies outside module")
        return source_start, source_end

    segments = sorted({int(item.segment) for item in relocations if int(item.segment)})
    if not segments:
        # Small ``bz`` modules store their data immediately before the
        # generic loader selected by initial CS.
        end = header.initial_cs * 16 if header.initial_cs else len(image)
        return 0x40, min(end, len(image))
    # The first non-zero relocation segment contains the module-side loader;
    # the last contains the generic BRUN loader. Compact initialized data is
    # stored after/around the former and before the latter.
    return segments[0] * 16, min(segments[-1] * 16, len(image))


def parse_direct_image(
    image: bytes,
    source_start: int,
    source_end: int,
    destination_start: int,
    destination_end: int,
) -> DataRun:
    """Recover strings from a bm module's exact initialized DGROUP image.

    QuickBASIC string descriptors are ordinary ``length,pointer`` pairs in
    this image.  In these modules their payload immediately follows the
    descriptor, so the relationship is unambiguous and can be recognized
    without interpreting arbitrary bytes as compact initializer records.
    Numeric values remain available byte-for-byte through the mapping emitted
    as ``initialized_data`` in the JSON report.
    """

    records: list[DataRecord] = []
    descriptor = destination_start
    while descriptor + 4 <= destination_end:
        descriptor_source = source_start + descriptor - destination_start
        logical_length, pointer = struct.unpack_from(
            "<HH", image, descriptor_source
        )
        if (
            logical_length > 0
            and pointer == descriptor + 4
            and pointer + logical_length <= destination_end
        ):
            payload_source = source_start + pointer - destination_start
            payload = image[payload_source : payload_source + logical_length]
            text_value = display_text(payload)
            if text_value is not None:
                records.append(DataRecord(
                    source_offset=payload_source,
                    length=logical_length,
                    destination=pointer,
                    payload_hex=payload.hex(),
                    text=text_value,
                    descriptor=descriptor,
                    mbf_single=None,
                    mbf_double=None,
                ))
                descriptor = pointer + ((logical_length + 1) & ~1)
                continue
        descriptor += 2
    return DataRun(
        "direct-image", source_start, source_end, source_end, tuple(records)
    )


def parse_compact_run(
    image: bytes,
    start: int,
    source_end: int,
    destination_start: int,
    destination_end: int,
) -> DataRun | None:
    position = start
    records: list[DataRecord] = []
    expected_destination: int | None = None
    while position + 4 <= source_end:
        length, destination = struct.unpack_from("<HH", image, position)
        if length == 0:
            # QuickBASIC emits some legitimate one-record initializer runs
            # (DUNSMALL's "PICK A COLOR PILL:" prompt is one).  Requiring two
            # records silently dropped those constants from the reconstructed
            # data segment.
            if not records:
                return None
            return DataRun("compact", start, position + 2, position, tuple(records))
        padded_length = (length + 1) & ~1
        if position + 4 + padded_length > source_end:
            return None
        if not destination_start <= destination < destination_end:
            return None
        if destination + length > 0x10000:
            return None
        if expected_destination is not None and destination != expected_destination:
            return None
        payload = image[position + 4 : position + 4 + length]
        text_value = display_text(payload)
        mbf_single, mbf_double = numeric_views(payload, text_value)
        records.append(DataRecord(
            source_offset=position,
            length=length,
            destination=destination,
            payload_hex=payload.hex(),
            text=text_value,
            descriptor=destination - 4,
            mbf_single=mbf_single,
            mbf_double=mbf_double,
        ))
        expected_destination = destination + 4 + padded_length
        position += 4 + padded_length
    return None


def parse_expanded_run(
    image: bytes,
    start: int,
    source_end: int,
    destination_start: int,
    destination_end: int,
) -> DataRun | None:
    position = start
    raw_records: list[tuple[int, int, int, bytes]] = []
    while position + 4 <= source_end:
        total_size, destination = struct.unpack_from("<HH", image, position)
        if total_size == 0:
            if len(raw_records) < 2:
                return None
            break
        if total_size < 4 or total_size & 1 or position + total_size > source_end:
            return None
        payload_length = total_size - 4
        if not destination_start <= destination < destination_end:
            return None
        if destination + payload_length > 0x10000:
            return None
        payload = image[position + 4 : position + total_size]
        raw_records.append((position, destination, payload_length, payload))
        position += total_size
    else:
        return None

    initialized = bytearray(0x10000)
    initialized_mask = bytearray(0x10000)
    for _, destination, payload_length, payload in raw_records:
        initialized[destination : destination + payload_length] = payload
        initialized_mask[destination : destination + payload_length] = b"\x01" * payload_length

    # Expanded bz streams store BASIC string descriptors as ordinary four-byte
    # records immediately before their padded character payload. The
    # descriptor contains the *logical* length, while the payload record is
    # word-aligned and can therefore contain one arbitrary padding byte. Find
    # these relationships before classifying four-byte records as numbers.
    string_descriptors: dict[int, tuple[int, int]] = {}
    for _, destination, payload_length, payload in raw_records:
        descriptor_offset = destination - 4
        if descriptor_offset < 0 or not all(initialized_mask[descriptor_offset:destination]):
            continue
        logical_length, pointer = struct.unpack_from("<HH", initialized, descriptor_offset)
        if (
            pointer == destination
            and logical_length <= payload_length
            and payload_length - logical_length <= 1
            and display_text(payload[:logical_length]) is not None
        ):
            string_descriptors[destination] = (descriptor_offset, logical_length)

    descriptor_destinations = {
        descriptor for descriptor, _ in string_descriptors.values()
    }
    records: list[DataRecord] = []
    for source_offset, destination, payload_length, payload in raw_records:
        descriptor: int | None = None
        logical_payload = payload
        if destination in string_descriptors:
            descriptor, logical_length = string_descriptors[destination]
            logical_payload = payload[:logical_length]
        text_value = display_text(logical_payload)
        if destination in descriptor_destinations:
            text_value = None
            mbf_single, mbf_double = None, None
        else:
            mbf_single, mbf_double = numeric_views(payload, text_value)
        records.append(DataRecord(
            source_offset=source_offset,
            length=payload_length,
            destination=destination,
            payload_hex=payload.hex(),
            text=text_value,
            descriptor=descriptor,
            mbf_single=mbf_single,
            mbf_double=mbf_double,
        ))
    return DataRun("expanded", start, position + 2, position, tuple(records))


def find_data_runs(
    header: MzHeader,
    image: bytes,
    relocations: list[object],
) -> list[DataRun]:
    destination_start, destination_end = module_data_limits(image)
    source_start, source_end = initializer_source_limits(header, image, relocations)
    if image[:2] == b"bm":
        return [parse_direct_image(
            image, source_start, source_end,
            destination_start, destination_end,
        )]
    is_expanded = len(image) >= 18 and image[16:18] == b"bz"
    parser = parse_expanded_run if is_expanded else parse_compact_run
    candidates: list[DataRun] = []
    for start in range(source_start, max(source_start, source_end - 3), 2):
        run = parser(image, start, source_end, destination_start, destination_end)
        if run is not None:
            candidates.append(run)

    # Every suffix of a valid run is structurally valid. Keep only maximal
    # source intervals so each original record is reported once.
    selected: list[DataRun] = []
    for candidate in sorted(candidates, key=lambda item: (-len(item.records), item.source_start)):
        if any(
            existing.source_start <= candidate.source_start
            and candidate.source_end <= existing.source_end
            for existing in selected
        ):
            continue
        selected.append(candidate)
    selected = sorted(selected, key=lambda item: item.source_start)
    if is_expanded and selected:
        # A bz module has one expanded initialization stream. Structural
        # suffixes and accidental short runs elsewhere in code are discarded.
        return [max(selected, key=lambda item: len(item.records))]
    return selected


def report_for(path: Path) -> dict[str, object]:
    header, relocations, image, _ = read_mz(path)
    code_start, code_end = auto_code_bounds(header, relocations, image)
    destination_start, destination_end = module_data_limits(image)
    source_start, source_end = initializer_source_limits(header, image, relocations)
    runs = find_data_runs(header, image, relocations)
    records = [record for run in runs for record in run.records]
    initialized_data = None
    if image[:2] == b"bm":
        payload = image[source_start:source_end]
        initialized_data = {
            "encoding": "direct-image",
            "source_start": source_start,
            "source_end": source_end,
            "destination_start": destination_start,
            "destination_end": destination_end,
            "source_to_destination_delta": source_start - destination_start,
            "payload_bytes": len(payload),
            "payload_sha256": hashlib.sha256(payload).hexdigest().upper(),
        }
    return {
        "format": "QuickBASIC 3 initialized data v2",
        "executable": str(path.resolve()),
        "code_start": code_start,
        "code_end": code_end,
        "source_scan_start": source_start,
        "source_scan_end": source_end,
        "data_destination_start": destination_start,
        "data_destination_end": destination_end,
        "initialized_data": initialized_data,
        "run_count": len(runs),
        "record_count": len(records),
        "text_record_count": sum(record.text is not None for record in records),
        "runs": [
            {
                "source_start": run.source_start,
                "source_end": run.source_end,
                "terminator_offset": run.terminator_offset,
                "encoding": run.encoding,
                "records": [
                    asdict(record) for record in run.records
                ],
            }
            for run in runs
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--json", type=Path, dest="json_path")
    args = parser.parse_args()
    report = report_for(args.executable)
    encoded = json.dumps(report, indent=2, ensure_ascii=False) + "\n"
    if args.json_path:
        args.json_path.parent.mkdir(parents=True, exist_ok=True)
        args.json_path.write_text(encoded, encoding="utf-8")
        print(args.json_path)
    else:
        print(encoded, end="")
    return 0


if __name__ == "__main__":
    sys.exit(main())
