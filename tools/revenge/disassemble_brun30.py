#!/usr/bin/env python3
"""Statically recover BRUN30 handler control flow used by a QB3 program."""

from __future__ import annotations

import argparse
import json
import struct
import sys
from collections import defaultdict, deque
from dataclasses import asdict, dataclass
from pathlib import Path

try:
    from capstone import CS_ARCH_X86, CS_GRP_CALL, CS_GRP_JUMP, CS_GRP_RET, CS_MODE_16, Cs
    from capstone.x86_const import X86_INS_JMP, X86_OP_IMM
except ImportError as exc:  # pragma: no cover
    raise SystemExit("capstone is required: python -m pip install capstone") from exc

from disassemble_qb3 import KNOWN_SELECTORS, Qb3Disassembler, RuntimeResolver, auto_code_bounds, read_mz


@dataclass
class Instruction:
    address: int
    size: int
    raw_hex: str
    mnemonic: str
    operands: str


def direct_target(item: object) -> int | None:
    operands = getattr(item, "operands", [])
    if len(operands) != 1 or operands[0].type != X86_OP_IMM:
        return None
    return int(operands[0].imm) & 0xFFFF


def recover_handlers(
    image: bytes, seeds: list[int]
) -> tuple[dict[int, Instruction], set[int]]:
    md = Cs(CS_ARCH_X86, CS_MODE_16)
    md.detail = True
    core_end = min(len(image), 0x10000)
    pending = deque(seed for seed in seeds if 0 <= seed < core_end)
    labels = set(pending)
    decoded: dict[int, Instruction] = {}
    occupied: set[int] = set()

    while pending:
        pc = pending.popleft()
        while 0 <= pc < core_end and pc not in decoded and pc not in occupied:
            items = list(md.disasm(image[pc:core_end], pc, count=1))
            if not items:
                break
            item = items[0]
            span = range(pc, pc + item.size)
            if any(byte in occupied for byte in span):
                break
            decoded[pc] = Instruction(
                address=pc,
                size=item.size,
                raw_hex=item.bytes.hex(" ").upper(),
                mnemonic=item.mnemonic,
                operands=item.op_str,
            )
            occupied.update(span)
            next_pc = pc + item.size
            is_call = item.group(CS_GRP_CALL)
            is_jump = item.group(CS_GRP_JUMP)
            target = direct_target(item) if is_call or is_jump else None
            if target is not None and target < core_end:
                labels.add(target)
                pending.append(target)
            if item.group(CS_GRP_RET):
                break
            if is_jump and item.id == X86_INS_JMP:
                break
            pc = next_pc
    return decoded, labels


def recover_output_vector_seeds(
    image: bytes, dgroup_image_base: int
) -> tuple[list[int], list[dict[str, object]]]:
    """Read the indirect PRINT vector sources from BRUN30's DGROUP image.

    BRUN30 9B2D and 9B84 copy seven words from DS:03A2 and DS:03B0 to
    DS:0EBA. The formatter then calls those words indirectly. The runtime's
    nonzero MZ relocation segment identifies DGROUP's physical image base;
    using same-numbered offsets in the code segment would be incorrect.
    """
    seeds: list[int] = []
    tables: list[dict[str, object]] = []
    core_end = min(len(image), 0x10000)
    for source, name, copy_routine in (
        (0x03A2, "print_statement_restore_vectors", "9B2D"),
        (0x03B0, "print_statement_active_vectors", "9B84"),
    ):
        physical = dgroup_image_base + source
        if physical + 14 > len(image):
            continue
        words = list(struct.unpack_from("<7H", image, physical))
        seeds.extend(target for target in words if 0 < target < core_end)
        tables.append({
            "name": name,
            "source": f"DS:{source:04X}",
            "physical_image_offset": f"{physical:05X}",
            "destination": "DS:0EBA-0EC6",
            "copy_routine": copy_routine,
            "targets": [f"{target:04X}" for target in words],
        })
    return seeds, tables


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("runtime", type=Path)
    parser.add_argument(
        "--symbols",
        type=Path,
        help="JSON from map_brun30_symbols.py for exact Microsoft public names",
    )
    parser.add_argument("--program", action="append", type=Path, required=True,
                        help="QB3 executable whose used selectors should seed analysis")
    parser.add_argument("--asm", type=Path, dest="asm_path", required=True)
    parser.add_argument("--json", type=Path, dest="json_path")
    parser.add_argument(
        "--seed", action="append", default=[],
        help="additional BRUN30 code offset to recover (hex, repeatable)",
    )
    args = parser.parse_args()

    runtime_header, runtime_relocations, runtime_image, _ = read_mz(args.runtime)
    resolver = RuntimeResolver(args.runtime, args.symbols)
    selectors: dict[tuple[int, int], dict[str, object]] = {}
    aliases: dict[int, list[str]] = defaultdict(list)
    for program in args.program:
        header, relocations, image, _ = read_mz(program)
        start, end = auto_code_bounds(header, relocations, image)
        scanner = Qb3Disassembler(image, start, end, resolver)
        program_code, _ = scanner.recursive([start])
        for call in program_code.values():
            if call.runtime_interrupt is None:
                continue
            assert call.runtime_interrupt is not None and call.runtime_selector is not None
            key = (call.runtime_interrupt, call.runtime_selector)
            target = call.runtime_target
            record = selectors.setdefault(key, {
                "interrupt": f"{key[0]:02X}",
                "selector": f"{key[1]:02X}",
                "known_name": KNOWN_SELECTORS.get(key),
                "target": None if target is None else f"{target:04X}",
                "brun30_symbols": resolver.symbol_names(target),
                "programs": set(),
                "sites": [],
            })
            record["programs"].add(program.name)  # type: ignore[union-attr]
            if len(record["sites"]) < 32:  # type: ignore[arg-type]
                record["sites"].append(f"{program.name}:{call.address:04X}")  # type: ignore[union-attr]
            if target is not None:
                label = f"int{key[0]:02x}_{key[1]:02x}"
                known = KNOWN_SELECTORS.get(key)
                if known:
                    label += "_" + known.lower()
                if label not in aliases[target]:
                    aliases[target].append(label)

    runtime_entry = runtime_header.initial_cs * 16 + runtime_header.initial_ip
    aliases[runtime_entry].append("runtime_entry")
    for seed_text in args.seed:
        try:
            seed = int(seed_text, 16)
        except ValueError as exc:
            parser.error(f"invalid hexadecimal --seed {seed_text!r}: {exc}")
        if not 0 <= seed < min(len(runtime_image), 0x10000):
            parser.error(f"--seed {seed_text!r} is outside the 16-bit code image")
        aliases[seed].append(f"extra_seed_{seed:04X}")
    nonzero_relocation_segments = sorted({
        relocation.segment for relocation in runtime_relocations
        if relocation.segment != 0
    })
    runtime_dgroup_image_base = (
        nonzero_relocation_segments[-1] * 16
        if nonzero_relocation_segments else len(runtime_image)
    )
    output_vector_seeds, output_vector_tables = recover_output_vector_seeds(
        runtime_image, runtime_dgroup_image_base
    )
    for table in output_vector_tables:
        for vector_index, target_text in enumerate(table["targets"]):
            target = int(target_text, 16)
            if target == 0:
                continue
            alias = f"{table['name']}_{vector_index}"
            if alias not in aliases[target]:
                aliases[target].append(alias)
    seeds = sorted(set(aliases) | set(output_vector_seeds))
    decoded, labels = recover_handlers(runtime_image, seeds)
    lines = [
        "; Static BRUN30 handler disassembly",
        "; Generated without executing BRUN30 or the analyzed programs.",
        f"; handler seeds={len(seeds)} recovered instructions={len(decoded)}",
        "",
    ]
    previous_end = 0
    for address in sorted(decoded):
        item = decoded[address]
        if address > previous_end:
            lines.append(f"; [{address - previous_end} undecoded byte(s): {previous_end:04X}..{address:04X}]")
        for alias in aliases.get(address, []):
            lines.append(alias + ":")
        for symbol in resolver.symbol_names(address):
            lines.append(f"; Microsoft QB3 public symbol: {symbol}")
        if address in labels and address not in aliases:
            lines.append(f"sub_{address:04X}:")
        lines.append(
            f"  {address:04X}  {item.raw_hex:<20} "
            f"{item.mnemonic:<9} {item.operands}"
        )
        previous_end = max(previous_end, address + item.size)

    args.asm_path.parent.mkdir(parents=True, exist_ok=True)
    args.asm_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    serial_selectors = []
    for key in sorted(selectors):
        record = selectors[key].copy()
        record["programs"] = sorted(record["programs"])
        serial_selectors.append(record)
    report = {
        "format": "BRUN30 static handler disassembly v1",
        "runtime": str(args.runtime.resolve()),
        "runtime_symbols": str(args.symbols.resolve()) if args.symbols else None,
        "mz": asdict(runtime_header),
        "relocations": [asdict(item) | {"image_offset": item.image_offset}
                        for item in runtime_relocations],
        "programs": [str(path.resolve()) for path in args.program],
        "selector_count": len(selectors),
        "handler_seed_count": len(seeds),
        "runtime_dgroup_image_base": f"{runtime_dgroup_image_base:05X}",
        "indirect_output_vector_tables": output_vector_tables,
        "recovered_instruction_count": len(decoded),
        "recovered_byte_count": sum(item.size for item in decoded.values()),
        "selectors": serial_selectors,
    }
    if args.json_path:
        args.json_path.parent.mkdir(parents=True, exist_ok=True)
        args.json_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    else:
        print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
