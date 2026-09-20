#!/usr/bin/env python3
"""Statically disassemble QuickBASIC 3 BRUN-linked DOS executables.

QuickBASIC 3 output is 16-bit x86 interleaved with compact calls to BRUN30.
INT 3Dh, 3Eh, and 3Fh are followed by a selector byte which the interrupt
handler consumes by changing the saved return address. A normal x86
disassembler therefore loses alignment after the first runtime call.

This tool never executes the analyzed program or runtime.
"""

from __future__ import annotations

import argparse
import bisect
import json
import re
import struct
import sys
from collections import Counter, deque
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Iterable

try:
    from capstone import CS_ARCH_X86, CS_GRP_CALL, CS_GRP_JUMP, CS_GRP_RET, CS_MODE_16, Cs
    from capstone.x86_const import X86_INS_JMP, X86_OP_IMM
except ImportError as exc:  # pragma: no cover
    raise SystemExit("capstone is required: python -m pip install capstone") from exc


RUNTIME_TABLES = {0x3D: 0x0171, 0x3E: 0x0243, 0x3F: 0x038D}

# Proven by the QB3 Hello World trace cited in tools/revenge/README.md. QB4
# and QBasic use different selector meanings and must not be mixed into this.
KNOWN_SELECTORS = {
    # BCOM30.LIB's sincos module exports $SIN at code offset 45h. Its fixed
    # opcode skeleton maps that module at BRUN30:BEC7, placing $SIN at BF0C,
    # the target of selector 3D:39.
    (0x3D, 0x39): "SIN",
    (0x3E, 0x02): "PROGRAM_END",
    (0x3E, 0x79): "STATEMENT_END",
    (0x3F, 0x6E): "PRINT_STRING",
    (0x3F, 0xBC): "PRINT_PREPARE",
    (0x3F, 0x43): "DIM_ARRAY_A",
    (0x3F, 0x44): "DIM_ARRAY_B",
    (0x3F, 0x45): "DIM_ARRAY_C",
    (0x3F, 0x46): "DIM_ARRAY_D",
    (0x3F, 0x58): "GOSUB_INLINE",
    (0x3F, 0x5D): "ON_GOSUB_TABLE",
    (0x3F, 0x5E): "ON_GOTO_TABLE",
    (0x3F, 0xB6): "INPUT_PREPARE",
    (0x3F, 0xB7): "INPUT_TYPE_LIST",
    (0x3F, 0xB8): "INPUT_ASSIGN_VALUE",
    # The Microsoft public name is $LENB, but its implementation dereferences
    # the string descriptor and returns the first character byte in BL.  Every
    # game module uses it as ASC(string), including comparisons with Return
    # (13), Escape (27), and lowercase ASCII.  Calling it a string length in
    # the semantic annotation is actively misleading for source recovery.
    (0x3F, 0xBB): "STRING_FIRST_BYTE_ASCII",
}

# These selectors pop the QB-generated far return address and read additional
# bytes directly from the caller's instruction stream before writing the
# advanced address back. Their widths are recovered from the matching BRUN30
# handlers, not inferred from surrounding x86. Keep the labels structural
# until each operation's higher-level BASIC meaning is independently proven.
INLINE_U8_SELECTORS = {
    (0x3F, 0x43),
    (0x3F, 0x44),
    (0x3F, 0x45),
    (0x3F, 0x46),
    (0x3F, 0x71),
    (0x3F, 0x72),
    # Generic numeric array operations read a compact element-type byte from
    # the caller stream (the 80h..84h values are visible in every game site).
    (0x3F, 0x85),
    (0x3F, 0x8D),
    (0x3F, 0x95),
    (0x3F, 0x9D),
    (0x3F, 0xA5),
    (0x3F, 0xA6),
    (0x3F, 0xAB),
    (0x3F, 0xAD),
    (0x3F, 0xB5),
}
INLINE_U16_SELECTORS = {(0x3F, 0x58)}
INLINE_WORD_TABLE_SELECTORS = {(0x3F, 0x5D), (0x3F, 0x5E)}

HEX_OPERAND_RE = re.compile(r"0x([0-9a-fA-F]+)")


@dataclass(frozen=True)
class MzHeader:
    last_page_bytes: int
    pages: int
    relocation_count: int
    header_paragraphs: int
    minimum_extra_paragraphs: int
    maximum_extra_paragraphs: int
    initial_ss: int
    initial_sp: int
    checksum: int
    initial_ip: int
    initial_cs: int
    relocation_offset: int
    overlay: int

    @property
    def header_bytes(self) -> int:
        return self.header_paragraphs * 16

    @property
    def declared_file_size(self) -> int:
        if self.pages == 0:
            return 0
        return (self.pages - 1) * 512 + (self.last_page_bytes or 512)


@dataclass(frozen=True)
class Relocation:
    offset: int
    segment: int

    @property
    def image_offset(self) -> int:
        return self.segment * 16 + self.offset


@dataclass
class DecodedInstruction:
    address: int
    size: int
    raw_hex: str
    mnemonic: str
    operands: str = ""
    comment: str = ""
    runtime_interrupt: int | None = None
    runtime_selector: int | None = None
    runtime_target: int | None = None
    flow_target: int | None = None
    far_segment: int | None = None
    far_offset: int | None = None
    far_image_target: int | None = None
    branch_targets: list[int] = field(default_factory=list)
    data_references: list[int] = field(default_factory=list)


@dataclass
class RecoveredData:
    strings: dict[int, str] = field(default_factory=dict)
    reserved_ranges: list[tuple[int, int]] = field(default_factory=list)
    source_start: int | None = None
    source_end: int | None = None
    destination_start: int | None = None
    destination_end: int | None = None
    source_to_destination_delta: int | None = None


def parse_int(value: str) -> int:
    return int(value, 0)


def read_mz(path: Path) -> tuple[MzHeader, list[Relocation], bytes, bytes]:
    raw = path.read_bytes()
    if len(raw) < 28 or raw[:2] != b"MZ":
        raise ValueError(f"{path} is not a DOS MZ executable")
    words = struct.unpack_from("<14H", raw)
    header = MzHeader(*words[1:])
    relocations = []
    for index in range(header.relocation_count):
        position = header.relocation_offset + index * 4
        if position + 4 > len(raw):
            raise ValueError(f"{path} has a truncated relocation table")
        offset, segment = struct.unpack_from("<HH", raw, position)
        relocations.append(Relocation(offset, segment))
    return header, relocations, raw[header.header_bytes :], raw


def module_name(image: bytes) -> str | None:
    if image[:2] == b"bm" and len(image) >= 10:
        return image[2:10].decode("cp437", errors="replace").rstrip(" \0")
    if len(image) >= 26 and image[16:18] == b"bz":
        return image[18:26].decode("cp437", errors="replace").rstrip(" \0")
    return None


def auto_code_bounds(
    header: MzHeader, relocations: Iterable[Relocation], image: bytes
) -> tuple[int, int]:
    if image[:2] == b"bm":
        start = header.initial_ip
    elif len(image) >= 18 and image[16:18] == b"bz":
        start = 0x40
    else:
        start = header.initial_cs * 16 + header.initial_ip

    nonzero_segments = sorted({item.segment for item in relocations if item.segment})
    if nonzero_segments:
        end = min(nonzero_segments) * 16
    elif header.initial_cs and header.initial_cs * 16 > start:
        end = header.initial_cs * 16
    else:
        end = len(image)
    return start, min(max(end, start), len(image))


class RuntimeResolver:
    def __init__(self, runtime_path: Path | None, symbol_path: Path | None = None):
        self.path = runtime_path
        self.symbol_path = symbol_path
        self.image = b""
        self.header: MzHeader | None = None
        self.symbols: dict[int, list[str]] = {}
        if runtime_path:
            self.header, _, self.image, _ = read_mz(runtime_path)
        if symbol_path:
            report = json.loads(symbol_path.read_text(encoding="utf-8"))
            for item in report.get("symbols", []):
                raw_address = item.get("runtime_address")
                name = item.get("name")
                if raw_address is None or not name:
                    continue
                address = int(str(raw_address), 16)
                names = self.symbols.setdefault(address, [])
                if str(name) not in names:
                    names.append(str(name))

    def resolve(self, interrupt: int, selector: int) -> int | None:
        if not self.image or interrupt not in RUNTIME_TABLES:
            return None
        if interrupt == 0x3D and selector >= 0x80:
            return None
        position = RUNTIME_TABLES[interrupt] + selector * 2
        if position + 2 > len(self.image):
            return None
        return struct.unpack_from("<H", self.image, position)[0]

    def symbol_names(self, address: int | None) -> list[str]:
        if address is None:
            return []
        return self.symbols.get(address, [])


class Qb3Disassembler:
    def __init__(
        self,
        image: bytes,
        code_start: int,
        code_end: int,
        runtime: RuntimeResolver,
        code_label_target_bias: int = 0,
    ):
        self.image = image
        self.code_start = code_start
        self.code_end = code_end
        self.runtime = runtime
        self.code_label_target_bias = code_label_target_bias
        self.md = Cs(CS_ARCH_X86, CS_MODE_16)
        self.md.detail = True

    def decode_one(self, address: int) -> tuple[DecodedInstruction, object | None]:
        if address + 3 <= self.code_end and self.image[address] == 0xCD:
            interrupt = self.image[address + 1]
            if interrupt in RUNTIME_TABLES:
                selector = self.image[address + 2]
                size = 3
                operands = f"{interrupt:02X}h, {selector:02X}h"
                branch_targets: list[int] = []
                if interrupt == 0x3D and selector >= 0x80 and address + 5 <= self.code_end:
                    parameter = struct.unpack_from("<H", self.image, address + 3)[0]
                    size = 5
                    operands += f", {parameter:04X}h"
                elif (interrupt, selector) in INLINE_U8_SELECTORS and address + 4 <= self.code_end:
                    parameter = self.image[address + 3]
                    size = 4
                    operands += f", u8={parameter:02X}h"
                elif (interrupt, selector) in INLINE_U16_SELECTORS and address + 5 <= self.code_end:
                    stored_parameter = struct.unpack_from(
                        "<H", self.image, address + 3
                    )[0]
                    parameter = (
                        stored_parameter + self.code_label_target_bias
                    ) & 0xFFFF
                    size = 5
                    operands += f", u16={stored_parameter:04X}h"
                    if self.code_label_target_bias:
                        operands += (
                            f", target={parameter:04X}h"
                            f", target_bias=+{self.code_label_target_bias:02X}h"
                        )
                    branch_targets.append(parameter)
                elif (
                    (interrupt, selector) in INLINE_WORD_TABLE_SELECTORS
                    and address + 4 <= self.code_end
                ):
                    count = self.image[address + 3]
                    candidate_size = 4 + count * 2
                    if address + candidate_size <= self.code_end:
                        stored_values = struct.unpack_from(
                            f"<{count}H", self.image, address + 4
                        )
                        values = tuple(
                            (value + self.code_label_target_bias) & 0xFFFF
                            for value in stored_values
                        )
                        size = candidate_size
                        operands += f", count={count:02X}h"
                        if stored_values:
                            operands += ", words=" + ",".join(
                                f"{value:04X}h" for value in stored_values
                            )
                            if self.code_label_target_bias:
                                operands += (
                                    f", target_bias=+{self.code_label_target_bias:02X}h"
                                )
                            branch_targets.extend(values)
                elif interrupt == 0x3F and selector == 0xB7 and address + 4 <= self.code_end:
                    # QB3's INPUT parser pops its far return address, reads a
                    # variable count and one compact type byte per destination,
                    # then writes the advanced return address back.
                    count = self.image[address + 3]
                    candidate_size = 4 + count
                    if address + candidate_size <= self.code_end:
                        types = self.image[address + 4 : address + candidate_size]
                        if all(1 <= value <= 4 for value in types):
                            size = candidate_size
                            operands += ", " + ",".join(f"{value:02X}" for value in types)
                target = self.runtime.resolve(interrupt, selector)
                details = []
                known = KNOWN_SELECTORS.get((interrupt, selector))
                if known:
                    details.append(known)
                if target is not None:
                    details.append(f"BRUN30:{target:04X}")
                    names = self.runtime.symbol_names(target)
                    if names:
                        details.append("symbols=" + ",".join(names))
                return DecodedInstruction(
                    address=address,
                    size=size,
                    raw_hex=self.image[address : address + size].hex(" ").upper(),
                    mnemonic="brun",
                    operands=operands,
                    comment="; ".join(details),
                    runtime_interrupt=interrupt,
                    runtime_selector=selector,
                    runtime_target=target,
                    branch_targets=branch_targets,
                ), None

        items = list(self.md.disasm(self.image[address : self.code_end], address, count=1))
        if not items:
            value = self.image[address]
            return DecodedInstruction(address, 1, f"{value:02X}", "db", f"{value:02X}h"), None
        item = items[0]
        operands = item.op_str
        flow_target = None
        far_segment = None
        far_offset = None
        far_image_target = None
        comment = ""
        # Capstone exposes an immediate for an x86 9Ah far CALL, but that
        # value does not retain the distinction between the encoded segment
        # and offset in a useful way for these segmented QB3 modules. Decode
        # the five-byte operand directly and report its physical offset in
        # the load image. Do not add it to the near control-flow graph: the
        # target belongs to a different compiler segment and may lie beyond
        # the module's ordinary near-code boundary.
        if item.bytes[0] == 0x9A and item.size >= 5:
            far_offset = struct.unpack_from("<H", item.bytes, 1)[0]
            far_segment = struct.unpack_from("<H", item.bytes, 3)[0]
            far_image_target = far_segment * 16 + far_offset
            operands = f"{far_segment:04X}h:{far_offset:04X}h"
            if far_image_target < len(self.image):
                comment = f"module image +{far_image_target:05X}h"
            else:
                comment = f"outside module image (+{far_image_target:05X}h)"
        elif item.group(CS_GRP_CALL) or item.group(CS_GRP_JUMP):
            flow_target = self.direct_target(item)
        if flow_target is not None:
            # Capstone does not wrap relative targets after IP FFFFh in
            # 16-bit mode. The processor does, so normalize the listing.
            operands = f"0x{flow_target:x}"
        return DecodedInstruction(
            address=address,
            size=item.size,
            raw_hex=item.bytes.hex(" ").upper(),
            mnemonic=item.mnemonic,
            operands=operands,
            comment=comment,
            flow_target=flow_target,
            far_segment=far_segment,
            far_offset=far_offset,
            far_image_target=far_image_target,
        ), item

    @staticmethod
    def direct_target(item: object) -> int | None:
        operands = getattr(item, "operands", [])
        if len(operands) != 1 or operands[0].type != X86_OP_IMM:
            return None
        return int(operands[0].imm) & 0xFFFF

    def recursive(self, seeds: Iterable[int]) -> tuple[dict[int, DecodedInstruction], set[int]]:
        pending = deque(seed for seed in seeds if self.code_start <= seed < self.code_end)
        decoded: dict[int, DecodedInstruction] = {}
        labels = set(pending)
        while pending:
            pc = pending.popleft()
            while self.code_start <= pc < self.code_end and pc not in decoded:
                instruction, capstone_item = self.decode_one(pc)
                decoded[pc] = instruction
                next_pc = pc + instruction.size
                if capstone_item is None:
                    for target in instruction.branch_targets:
                        if self.code_start <= target < self.code_end:
                            labels.add(target)
                            pending.append(target)
                    if (
                        instruction.runtime_interrupt,
                        instruction.runtime_selector,
                    ) == (0x3E, 0x02):
                        break
                    pc = next_pc
                    continue
                is_call = capstone_item.group(CS_GRP_CALL)
                is_jump = capstone_item.group(CS_GRP_JUMP)
                target = self.direct_target(capstone_item) if is_call or is_jump else None
                if target is not None and self.code_start <= target < self.code_end:
                    labels.add(target)
                    pending.append(target)
                if capstone_item.group(CS_GRP_RET):
                    break
                if is_jump and capstone_item.id == X86_INS_JMP:
                    break
                pc = next_pc
        return decoded, labels

    def scan_runtime_calls(self) -> list[DecodedInstruction]:
        calls = []
        address = self.code_start
        while address + 3 <= self.code_end:
            if self.image[address] == 0xCD and self.image[address + 1] in RUNTIME_TABLES:
                decoded, _ = self.decode_one(address)
                calls.append(decoded)
                address += decoded.size
            else:
                address += 1
        return calls


def format_listing(
    executable: Path,
    header: MzHeader,
    module: str | None,
    code_start: int,
    code_end: int,
    decoded: dict[int, DecodedInstruction],
    labels: set[int],
    function_names: dict[int, str],
) -> str:
    lines = [
        f"; QuickBASIC 3 static disassembly of {executable.name}",
        "; Generated without executing the DOS program.",
        f"; module={module or 'unknown'} code={code_start:04X}..{code_end:04X}",
        f"; entry={header.initial_cs:04X}:{header.initial_ip:04X}",
        "",
    ]
    previous_end = code_start
    for address in sorted(decoded):
        instruction = decoded[address]
        if address > previous_end:
            lines.append(f"; [{address - previous_end} undecoded byte(s): {previous_end:04X}..{address:04X}]")
        if address in labels:
            label = function_names.get(address, f"loc_{address:04X}")
            lines.append(label + ":")
        operation = f"{instruction.mnemonic:<9} {instruction.operands}".rstrip()
        comment = f" ; {instruction.comment}" if instruction.comment else ""
        lines.append(f"  {address:04X}  {instruction.raw_hex:<20} {operation:<34}{comment}")
        previous_end = max(previous_end, address + instruction.size)
    if previous_end < code_end:
        lines.append(f"; [{code_end - previous_end} undecoded byte(s): {previous_end:04X}..{code_end:04X}]")
    return "\n".join(lines) + "\n"


def selector_summary(
    calls: Iterable[DecodedInstruction], runtime: RuntimeResolver
) -> list[dict[str, object]]:
    counts = Counter((item.runtime_interrupt, item.runtime_selector) for item in calls)
    sites: dict[tuple[int | None, int | None], list[int]] = {}
    targets: dict[tuple[int | None, int | None], int | None] = {}
    for item in calls:
        key = (item.runtime_interrupt, item.runtime_selector)
        sites.setdefault(key, []).append(item.address)
        targets[key] = item.runtime_target
    result = []
    for (interrupt, selector), count in sorted(counts.items()):
        assert interrupt is not None and selector is not None
        target = targets[(interrupt, selector)]
        result.append({
            "interrupt": f"{interrupt:02X}",
            "selector": f"{selector:02X}",
            "known_name": KNOWN_SELECTORS.get((interrupt, selector)),
            "brun30_target": None if target is None else f"{target:04X}",
            "brun30_symbols": runtime.symbol_names(target),
            "count": count,
            "first_sites": [f"{site:04X}" for site in sites[(interrupt, selector)][:16]],
        })
    return result


def load_data_symbols(path: Path | None) -> RecoveredData:
    if path is None:
        return RecoveredData()
    report = json.loads(path.read_text(encoding="utf-8"))
    recovered = RecoveredData()
    initialized = report.get("initialized_data")
    if initialized:
        recovered.source_start = int(initialized["source_start"])
        recovered.source_end = int(initialized["source_end"])
        recovered.destination_start = int(initialized["destination_start"])
        recovered.destination_end = int(initialized["destination_end"])
        recovered.source_to_destination_delta = int(
            initialized["source_to_destination_delta"]
        )
    for run in report.get("runs", []):
        for record in run.get("records", []):
            text = record.get("text")
            descriptor = record.get("descriptor")
            if text is None or descriptor is None:
                continue
            escaped = str(text).encode("unicode_escape").decode("ascii")
            descriptor = int(descriptor)
            destination = int(record["destination"])
            length = int(record["length"])
            recovered.strings[descriptor] = escaped
            recovered.reserved_ranges.append(
                (descriptor, destination + ((length + 1) & ~1))
            )
    return recovered


def initialized_mbf_single(
    recovered: RecoveredData, image: bytes, address: int
) -> float | None:
    if (
        recovered.destination_start is None
        or recovered.destination_end is None
        or recovered.source_to_destination_delta is None
        or not recovered.destination_start <= address
        or address + 4 > recovered.destination_end
        or any(start <= address < end for start, end in recovered.reserved_ranges)
    ):
        return None
    source = address + recovered.source_to_destination_delta
    if source < 0 or source + 4 > len(image):
        return None
    payload = image[source : source + 4]
    exponent = payload[3]
    if exponent == 0:
        return 0.0
    # This deliberately rejects arbitrary instruction/string bytes that can
    # also form a mathematically valid MBF value. All constants referenced by
    # Revenge lie comfortably inside this exponent range.
    if not 96 <= exponent <= 160:
        return None
    mantissa = payload[0] | (payload[1] << 8) | ((payload[2] & 0x7F) << 16)
    value = (1.0 + mantissa / 8388608.0) * (2.0 ** (exponent - 129))
    if payload[2] & 0x80:
        value = -value
    if not -1.0e9 <= value <= 1.0e9:
        return None
    return value


def initialized_mbf_double(
    recovered: RecoveredData, image: bytes, address: int
) -> float | None:
    """Decode an initialized eight-byte Microsoft Binary Format value.

    QB3 uses separate single- and double-precision runtime entry points.  A
    double whose low four mantissa bytes are zero looks exactly like an MBF
    single zero if only its first four bytes are inspected.  That is common
    for Revenge's integral prices (10, 200, 1000, ...), so retaining a double
    view prevents those constants from being mislabeled as zero.
    """
    if (
        recovered.destination_start is None
        or recovered.destination_end is None
        or recovered.source_to_destination_delta is None
        or not recovered.destination_start <= address
        or address + 8 > recovered.destination_end
        or any(start <= address < end for start, end in recovered.reserved_ranges)
    ):
        return None
    source = address + recovered.source_to_destination_delta
    if source < 0 or source + 8 > len(image):
        return None
    payload = image[source : source + 8]
    exponent = payload[7]
    if exponent == 0:
        return 0.0
    if not 96 <= exponent <= 160:
        return None
    mantissa_bytes = bytearray(payload[:7])
    negative = bool(mantissa_bytes[6] & 0x80)
    mantissa_bytes[6] &= 0x7F
    mantissa = int.from_bytes(mantissa_bytes, "little")
    value = (1.0 + mantissa / float(1 << 55)) * (2.0 ** (exponent - 129))
    if negative:
        value = -value
    if not -1.0e15 <= value <= 1.0e15:
        return None
    return value


def load_source_map(
    path: Path | None, module: str | None
) -> tuple[dict[int, str], dict[int, str]]:
    if path is None or module is None:
        return {}, {}
    report = json.loads(path.read_text(encoding="utf-8"))
    module_report = report.get("modules", {}).get(module, {})
    functions: dict[int, str] = {}
    data: dict[int, str] = {}
    for raw_address, item in module_report.get("functions", {}).items():
        name = item.get("name") if isinstance(item, dict) else item
        if name:
            functions[int(raw_address, 16)] = str(name)
    for raw_address, item in module_report.get("data", {}).items():
        name = item.get("name") if isinstance(item, dict) else item
        if name:
            data[int(raw_address, 16)] = str(name)
    return functions, data


def annotate_data_references(
    decoded: dict[int, DecodedInstruction],
    recovered: RecoveredData,
    named_data: dict[int, str],
    image: bytes,
) -> None:
    symbols = recovered.strings
    if not symbols and not named_data and recovered.destination_start is None:
        return
    for instruction in decoded.values():
        if (
            instruction.mnemonic.startswith("j")
            or instruction.mnemonic.startswith("loop")
            or instruction.mnemonic in {"call", "lcall", "ret", "retf", "iret"}
        ):
            continue
        references = []
        for match in HEX_OPERAND_RE.finditer(instruction.operands):
            address = int(match.group(1), 16)
            numeric = initialized_mbf_single(recovered, image, address)
            precision = "SINGLE"
            double_numeric = initialized_mbf_double(recovered, image, address)
            # An initialized MBF double commonly begins with four zero bytes.
            # The same address can therefore legitimately be a QB single zero
            # or a nonzero QB double. The operand alone does not carry enough
            # type information to choose between them; retaining both views is
            # safer than silently assigning the double value to single code.
            if numeric == 0.0 and double_numeric not in (None, 0.0):
                numeric = double_numeric
                precision = "SINGLE_0_OR_DOUBLE"
            if (address in symbols or address in named_data or numeric is not None) and (
                address not in [item[0] for item in references]
            ):
                references.append((address, symbols.get(address), numeric, precision))
        if references:
            instruction.data_references.extend(address for address, _, _, _ in references)
            comments = []
            for address, value, numeric, precision in references:
                if address in named_data:
                    comments.append(f"DS:{address:04X} {named_data[address]}")
                if value is not None:
                    comments.append(f'DS:{address:04X} STRING "{value}"')
                if numeric is not None:
                    rendered = (
                        str(int(numeric)) if numeric.is_integer()
                        else format(numeric, ".9g")
                    )
                    if precision == "SINGLE_0_OR_DOUBLE":
                        comments.append(
                            f"DS:{address:04X} INITIAL_MBF_SINGLE 0 OR "
                            f"INITIAL_MBF_DOUBLE {rendered}"
                        )
                    else:
                        comments.append(
                            f"DS:{address:04X} INITIAL_MBF_{precision} {rendered}"
                        )
            suffix = "; ".join(comments)
            instruction.comment = "; ".join(filter(None, (instruction.comment, suffix)))


def build_function_inventory(
    decoded: dict[int, DecodedInstruction],
    code_start: int,
    code_end: int,
    symbols: dict[int, str],
    runtime: RuntimeResolver,
    function_names: dict[int, str],
    named_data: dict[int, str],
) -> list[dict[str, object]]:
    """Build a conservative direct-call procedure inventory.

    QB3 emits ordinary near CALLs for BASIC procedures and local helpers. A
    target is treated as a procedure start only when a decoded direct CALL
    reaches it; computed branch-table destinations remain labels within their
    containing procedure.
    """
    starts = {code_start}
    callers_by_target: dict[int, list[int]] = {}
    for address, instruction in decoded.items():
        if instruction.mnemonic not in {"call", "lcall"}:
            continue
        target = instruction.flow_target
        if target is None or not code_start <= target < code_end:
            continue
        starts.add(target)
        callers_by_target.setdefault(target, []).append(address)

    ordered_starts = sorted(starts)
    instructions_by_function: dict[int, list[DecodedInstruction]] = {
        start: [] for start in ordered_starts
    }
    for address in sorted(decoded):
        index = bisect.bisect_right(ordered_starts, address) - 1
        if index >= 0:
            instructions_by_function[ordered_starts[index]].append(decoded[address])

    inventory = []
    for index, start in enumerate(ordered_starts):
        end = ordered_starts[index + 1] if index + 1 < len(ordered_starts) else code_end
        instructions = instructions_by_function[start]
        selector_counts = Counter(
            (instruction.runtime_interrupt, instruction.runtime_selector)
            for instruction in instructions
            if instruction.runtime_interrupt is not None
        )
        referenced_strings = []
        seen_strings = set()
        for instruction in instructions:
            for address in instruction.data_references:
                if address in seen_strings:
                    continue
                seen_strings.add(address)
                referenced_strings.append({
                    "descriptor": f"{address:04X}",
                    "text": symbols.get(address, ""),
                    "source_name": named_data.get(address),
                })
        runtime_calls = []
        for (interrupt, selector), count in sorted(selector_counts.items()):
            assert interrupt is not None and selector is not None
            runtime_calls.append({
                "interrupt": f"{interrupt:02X}",
                "selector": f"{selector:02X}",
                "known_name": KNOWN_SELECTORS.get((interrupt, selector)),
                "brun30_target": (
                    None
                    if runtime.resolve(interrupt, selector) is None
                    else f"{runtime.resolve(interrupt, selector):04X}"
                ),
                "brun30_symbols": runtime.symbol_names(
                    runtime.resolve(interrupt, selector)
                ),
                "count": count,
            })
        inventory.append({
            "start": f"{start:04X}",
            "source_name": function_names.get(start),
            "end_exclusive": f"{end:04X}",
            "decoded_instruction_count": len(instructions),
            "caller_sites": [
                f"{address:04X}" for address in sorted(callers_by_target.get(start, []))
            ],
            "referenced_strings": referenced_strings,
            "runtime_calls": runtime_calls,
        })
    return inventory


def build_instruction_report(
    decoded: dict[int, DecodedInstruction],
    functions: list[dict[str, object]],
    runtime: RuntimeResolver,
) -> list[dict[str, object]]:
    """Serialize the recovered instruction graph for follow-on static tools.

    The ordinary report intentionally stays compact.  This separate stream is
    the lossless, machine-readable equivalent of the annotated assembly and is
    used by the graphics/control-flow extractors.  It contains only statically
    decoded state; producing it never starts the DOS program or BRUN30.
    """
    starts = [int(str(item["start"]), 16) for item in functions]
    function_by_start = {
        int(str(item["start"]), 16): item for item in functions
    }
    report: list[dict[str, object]] = []
    for address in sorted(decoded):
        instruction = decoded[address]
        function_index = bisect.bisect_right(starts, address) - 1
        function = (
            function_by_start[starts[function_index]]
            if function_index >= 0 else None
        )
        item: dict[str, object] = {
            "address": f"{instruction.address:04X}",
            "size": instruction.size,
            "bytes": instruction.raw_hex,
            "mnemonic": instruction.mnemonic,
            "operands": instruction.operands,
            "comment": instruction.comment,
            "function_start": function["start"] if function else None,
            "function_name": function.get("source_name") if function else None,
            "flow_target": (
                None if instruction.flow_target is None
                else f"{instruction.flow_target:04X}"
            ),
            "branch_targets": [
                f"{target:04X}" for target in instruction.branch_targets
            ],
            "data_references": [
                f"{target:04X}" for target in instruction.data_references
            ],
        }
        if instruction.runtime_interrupt is not None:
            item["runtime"] = {
                "interrupt": f"{instruction.runtime_interrupt:02X}",
                "selector": f"{instruction.runtime_selector:02X}",
                "known_name": KNOWN_SELECTORS.get(
                    (instruction.runtime_interrupt,
                     instruction.runtime_selector)
                ),
                "brun30_target": (
                    None if instruction.runtime_target is None
                    else f"{instruction.runtime_target:04X}"
                ),
                "brun30_symbols": runtime.symbol_names(
                    instruction.runtime_target
                ),
            }
        report.append(item)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--runtime", type=Path, help="matching BRUN30.EXE for selector resolution")
    parser.add_argument(
        "--runtime-symbols",
        type=Path,
        help="JSON from map_brun30_symbols.py for exact Microsoft runtime names",
    )
    parser.add_argument("--data", type=Path, help="JSON from extract_qb3_data.py for data xrefs")
    parser.add_argument(
        "--source-map",
        type=Path,
        help="manually verified Revenge function/data names",
    )
    parser.add_argument("--start", type=parse_int, help="override code start offset")
    parser.add_argument("--end", type=parse_int, help="override code end offset")
    parser.add_argument("--seed", action="append", type=parse_int, default=[], help="additional control-flow seed")
    parser.add_argument("--asm", type=Path, dest="asm_path")
    parser.add_argument("--json", type=Path, dest="json_path")
    parser.add_argument(
        "--instructions-json",
        type=Path,
        dest="instructions_json_path",
        help="write every statically recovered instruction as JSON",
    )
    args = parser.parse_args()

    header, relocations, image, _ = read_mz(args.executable)
    auto_start, auto_end = auto_code_bounds(header, relocations, image)
    code_start = auto_start if args.start is None else args.start
    code_end = auto_end if args.end is None else args.end
    if not 0 <= code_start < code_end <= len(image):
        parser.error(f"invalid code range {code_start:#x}..{code_end:#x} for {len(image):#x}-byte image")

    if args.runtime_symbols and not args.runtime:
        parser.error("--runtime-symbols requires --runtime")
    runtime = RuntimeResolver(args.runtime, args.runtime_symbols)
    current_module = module_name(image)
    function_names, named_data = load_source_map(args.source_map, current_module)
    # BRUN-expanded `bz` modules retain BASIC code-label offsets relative to
    # the module payload beginning at image +10h.  Ordinary x86 relative
    # branches have already been fixed by the linker, but the words embedded
    # after QB3's GOSUB, ON GOTO, and ON GOSUB selectors have not. BEGIN.EXE
    # proves the adjustment at all five menu destinations. NCD.EXE proves the
    # same adjustment independently for its three inline GOSUB destinations:
    # 04DD->04ED, 04F5->0505, and 0511->0521 all land on clean routine starts.
    # Compact `bm` modules such as DUNSMALL store physical image offsets and
    # therefore use no bias.
    code_label_target_bias = 0x10 if image[16:18] == b"bz" else 0
    disassembler = Qb3Disassembler(
        image, code_start, code_end, runtime, code_label_target_bias
    )
    decoded, labels = disassembler.recursive([code_start, *args.seed])
    recovered_data = load_data_symbols(args.data)
    data_symbols = recovered_data.strings
    annotate_data_references(decoded, recovered_data, named_data, image)
    calls = [
        instruction
        for instruction in decoded.values()
        if instruction.runtime_interrupt is not None
    ]
    branch_tables = [
        {
            "site": f"{instruction.address:04X}",
            "interrupt": f"{instruction.runtime_interrupt:02X}",
            "selector": f"{instruction.runtime_selector:02X}",
            "targets": [f"{target:04X}" for target in instruction.branch_targets],
        }
        for instruction in decoded.values()
        if instruction.branch_targets
    ]
    far_calls = [
        {
            "site": f"{instruction.address:04X}",
            "segment": f"{instruction.far_segment:04X}",
            "offset": f"{instruction.far_offset:04X}",
            "image_target": f"{instruction.far_image_target:05X}",
            "inside_module_image": instruction.far_image_target < len(image),
        }
        for instruction in decoded.values()
        if instruction.far_segment is not None
        and instruction.far_offset is not None
        and instruction.far_image_target is not None
    ]
    functions = build_function_inventory(
        decoded, code_start, code_end, data_symbols, runtime,
        function_names, named_data,
    )
    listing = format_listing(
        args.executable, header, current_module, code_start, code_end,
        decoded, labels, function_names,
    )
    report = {
        "format": "QuickBASIC 3 BRUN static disassembly v1",
        "executable": str(args.executable.resolve()),
        "runtime": str(args.runtime.resolve()) if args.runtime else None,
        "runtime_symbols": (
            str(args.runtime_symbols.resolve()) if args.runtime_symbols else None
        ),
        "module": current_module,
        "mz": asdict(header),
        "relocations": [asdict(item) | {"image_offset": item.image_offset} for item in relocations],
        "code_start": code_start,
        "code_end": code_end,
        "code_label_target_bias": code_label_target_bias,
        # Retained for readers of the v1 report schema.  In expanded modules
        # the same bias applies to inline GOSUB as well as word tables.
        "word_table_target_bias": code_label_target_bias,
        "reachable_instruction_count": len(decoded),
        "reachable_byte_count": sum(item.size for item in decoded.values()),
        "annotated_data_symbol_count": len(data_symbols),
        "source_mapped_function_count": len(function_names),
        "source_mapped_data_count": len(named_data),
        "runtime_selectors_in_code_range": selector_summary(calls, runtime),
        "computed_branch_tables": branch_tables,
        "direct_far_calls": far_calls,
        "direct_call_function_inventory": functions,
    }
    if args.asm_path:
        args.asm_path.parent.mkdir(parents=True, exist_ok=True)
        args.asm_path.write_text(listing, encoding="utf-8")
    else:
        print(listing, end="")
    if args.json_path:
        args.json_path.parent.mkdir(parents=True, exist_ok=True)
        args.json_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    elif args.asm_path:
        print(json.dumps(report, indent=2))
    if args.instructions_json_path:
        args.instructions_json_path.parent.mkdir(parents=True, exist_ok=True)
        instruction_report = {
            "format": "QuickBASIC 3 BRUN static instruction graph v1",
            "executable": str(args.executable.resolve()),
            "runtime": str(args.runtime.resolve()) if args.runtime else None,
            "module": current_module,
            "code_start": code_start,
            "code_end": code_end,
            "instruction_count": len(decoded),
            "instructions": build_instruction_report(
                decoded, functions, runtime
            ),
        }
        args.instructions_json_path.write_text(
            json.dumps(instruction_report, indent=2) + "\n",
            encoding="utf-8",
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
