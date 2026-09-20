#!/usr/bin/env python3
"""Audit explicit native-port evidence against every recovered QB3 routine.

This is intentionally conservative. A routine name in the source map, an
address in a comment, or a passing native self-test does not by itself prove
equivalence. Only records reviewed into revenge_parity_evidence.json can move
out of the unresolved state.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter
from pathlib import Path


VALID_STATUS = {"verified", "adapted", "unresolved"}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--root", type=Path,
        default=Path(__file__).resolve().parents[2],
        help="repository root containing port/ and tools/revenge/",
    )
    parser.add_argument(
        "--json", action="store_true",
        help="emit the complete machine-readable matrix instead of a summary",
    )
    parser.add_argument(
        "--require-certified", action="store_true",
        help="return failure unless every recovered routine is verified",
    )
    return parser.parse_args()


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def collect_port_text(root: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    for directory in (root / "port" / "src", root / "port" / "include"):
        for path in sorted(directory.rglob("*")):
            if path.suffix.lower() not in {".c", ".h"}:
                continue
            result[path.relative_to(root).as_posix()] = path.read_text(
                encoding="utf-8", errors="replace"
            )
    return result


def address_references(address: str, port_text: dict[str, str]) -> list[str]:
    # Address references are discovery aids only. They never upgrade status.
    token = re.compile(rf"(?i)(?<![0-9a-f]){re.escape(address)}(?![0-9a-f])")
    return [name for name, text in port_text.items() if token.search(text)]


def main() -> int:
    args = parse_args()
    root = args.root.resolve()
    revenge = root / "tools" / "revenge"
    source_map = load_json(revenge / "revenge_source_map.json")
    evidence_doc = load_json(revenge / "revenge_parity_evidence.json")
    evidence = evidence_doc.get("routines", {})
    port_text = collect_port_text(root)

    known_keys: set[str] = set()
    matrix: list[dict] = []
    errors: list[str] = []
    for module_name, module in source_map["modules"].items():
        for address, function in module.get("functions", {}).items():
            key = f"{module_name}:{address.upper()}"
            known_keys.add(key)
            record = evidence.get(key, {})
            status = record.get("status", "unresolved")
            if status not in VALID_STATUS:
                errors.append(f"{key}: invalid status {status!r}")
                status = "unresolved"
            entry = {
                "key": key,
                "module": module_name,
                "address": address.upper(),
                "name": function["name"],
                "recovery_confidence": function.get("confidence", "unknown"),
                "status": status,
                "evidence": record.get("evidence", []),
                "tests": record.get("tests", []),
                "notes": record.get("notes", ""),
                "port_address_references": address_references(address, port_text),
            }
            matrix.append(entry)

    for key in sorted(set(evidence) - known_keys):
        errors.append(f"evidence names unknown routine {key}")
    for key, record in evidence.items():
        if record.get("status") in {"verified", "adapted"} and not record.get(
            "evidence"
        ):
            errors.append(f"{key}: non-unresolved status has no evidence")

    counts = Counter(item["status"] for item in matrix)
    modules = {
        name: dict(Counter(
            item["status"] for item in matrix if item["module"] == name
        ))
        for name in source_map["modules"]
    }
    certified = not errors and counts["verified"] == len(matrix)
    report = {
        "schema": "Moraff's Revenge parity matrix v1",
        "certified_1_to_1": certified,
        "routine_count": len(matrix),
        "status_counts": dict(counts),
        "module_status_counts": modules,
        "errors": errors,
        "routines": matrix,
    }

    if args.json:
        json.dump(report, sys.stdout, indent=2)
        print()
    else:
        print(f"Recovered routines: {len(matrix)}")
        print("Status: " + ", ".join(
            f"{name}={counts[name]}"
            for name in ("verified", "adapted", "unresolved")
        ))
        for module, values in modules.items():
            print(f"  {module}: " + ", ".join(
                f"{name}={values.get(name, 0)}"
                for name in ("verified", "adapted", "unresolved")
            ))
        if errors:
            print("Evidence errors:")
            for error in errors:
                print(f"  - {error}")
        print("1:1 certified: " + ("YES" if certified else "NO"))

    if errors or (args.require_certified and not certified):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
