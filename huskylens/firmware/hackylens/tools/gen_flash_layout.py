#!/usr/bin/env python3
"""Validate the canonical SPI-flash map and generate its C constants."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import sys
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
NAME_RE = re.compile(r"^[a-z][a-z0-9_]*$")
ROOT_FIELDS = {"schema_version", "flash", "partitions"}
FLASH_FIELDS = {
    "address_bytes", "minimum_capacity", "maximum_capacity",
    "erase_size", "program_size",
}
PARTITION_FIELDS = {
    "name", "offset", "size", "required_capacity", "runtime_writable",
    "description",
}


def strict_fields(value: dict[str, Any], allowed: set[str], field: str) -> None:
    unknown = sorted(set(value) - allowed)
    if unknown:
        raise ValueError(f"{field}: unknown field(s): {', '.join(unknown)}")


def number(value: Any, field: str) -> int:
    if isinstance(value, bool):
        raise ValueError(f"{field}: boolean is not an integer")
    if isinstance(value, int):
        result = value
    elif isinstance(value, str):
        try:
            result = int(value, 0)
        except ValueError as exc:
            raise ValueError(f"{field}: invalid integer {value!r}") from exc
    else:
        raise ValueError(f"{field}: expected integer or integer string")
    if result < 0 or result > 0xFFFFFFFF:
        raise ValueError(f"{field}: value is outside uint32_t")
    return result


def canonical_json_bytes(value: Any) -> bytes:
    """Return the exact schema-1 canonical JSON byte representation."""

    return (
        json.dumps(
            value,
            ensure_ascii=False,
            allow_nan=False,
            sort_keys=True,
            indent=2,
            separators=(",", ": "),
        ).encode("utf-8")
        + b"\n"
    )


def load_layout(path: Path, *, require_canonical: bool = True) -> dict[str, Any]:
    try:
        raw = path.read_bytes()
        if raw.startswith(b"\xef\xbb\xbf"):
            raise ValueError("UTF-8 BOM is forbidden")
        text = raw.decode("utf-8")
        layout = json.loads(
            text,
            parse_constant=lambda value: (_ for _ in ()).throw(
                ValueError(f"non-finite JSON number {value}")
            ),
        )
    except (OSError, UnicodeDecodeError, json.JSONDecodeError, ValueError) as exc:
        raise ValueError(f"cannot read {path}: {exc}") from exc
    if not isinstance(layout, dict):
        raise ValueError("root: expected object")
    strict_fields(layout, ROOT_FIELDS, "root")
    if type(layout.get("schema_version")) is not int or layout["schema_version"] != 1:
        raise ValueError("schema_version: expected 1")
    if require_canonical and raw != canonical_json_bytes(layout):
        raise ValueError(
            "file bytes are not canonical UTF-8 JSON (sorted keys, LF, "
            "no trailing spaces, exactly one final LF)"
        )
    return layout


def validate(layout: dict[str, Any]) -> tuple[dict[str, int], list[dict[str, Any]]]:
    raw_flash = layout.get("flash")
    raw_partitions = layout.get("partitions")
    if not isinstance(raw_flash, dict):
        raise ValueError("flash: expected object")
    strict_fields(raw_flash, FLASH_FIELDS, "flash")
    if not isinstance(raw_partitions, list) or not raw_partitions:
        raise ValueError("partitions: expected a non-empty array")

    flash = {
        key: number(raw_flash.get(key), f"flash.{key}")
        for key in ("address_bytes", "minimum_capacity", "maximum_capacity",
                    "erase_size", "program_size")
    }
    if flash["address_bytes"] != 3:
        raise ValueError("flash.address_bytes: this driver supports exactly 3")
    if not flash["erase_size"] or not flash["program_size"]:
        raise ValueError("flash: erase_size and program_size must be non-zero")
    if flash["erase_size"] % flash["program_size"]:
        raise ValueError("flash: erase_size must be a multiple of program_size")
    if flash["minimum_capacity"] > flash["maximum_capacity"]:
        raise ValueError("flash: minimum_capacity exceeds maximum_capacity")
    address_limit = 1 << (8 * flash["address_bytes"])
    if flash["maximum_capacity"] > address_limit:
        raise ValueError("flash: maximum_capacity exceeds address command width")

    partitions: list[dict[str, Any]] = []
    seen: set[str] = set()
    previous_end = 0
    for index, raw in enumerate(raw_partitions):
        field = f"partitions[{index}]"
        if not isinstance(raw, dict):
            raise ValueError(f"{field}: expected object")
        strict_fields(raw, PARTITION_FIELDS, field)
        name = raw.get("name")
        if not isinstance(name, str) or not NAME_RE.fullmatch(name):
            raise ValueError(f"{field}.name: expected lower_snake_case identifier")
        if name in seen:
            raise ValueError(f"{field}.name: duplicate {name!r}")
        seen.add(name)
        offset = number(raw.get("offset"), f"{field}.offset")
        size = number(raw.get("size"), f"{field}.size")
        required = number(raw.get("required_capacity"), f"{field}.required_capacity")
        writable = raw.get("runtime_writable")
        description = raw.get("description")
        if not isinstance(writable, bool):
            raise ValueError(f"{field}.runtime_writable: expected boolean")
        if not isinstance(description, str) or not description:
            raise ValueError(f"{field}.description: expected non-empty string")
        if not size:
            raise ValueError(f"{field}.size: must be non-zero")
        if offset % flash["erase_size"] or size % flash["erase_size"]:
            raise ValueError(f"{field}: offset and size must be erase-aligned")
        end = offset + size
        if end > 0xFFFFFFFF or end > flash["maximum_capacity"]:
            raise ValueError(f"{field}: partition exceeds maximum flash capacity")
        if offset < previous_end:
            raise ValueError(f"{field}: overlaps the preceding partition")
        if required < end or required > flash["maximum_capacity"]:
            raise ValueError(f"{field}.required_capacity: does not contain partition")
        if required not in (flash["minimum_capacity"], flash["maximum_capacity"]):
            raise ValueError(f"{field}.required_capacity: unsupported capacity profile")
        partitions.append({
            "name": name,
            "offset": offset,
            "size": size,
            "required_capacity": required,
            "runtime_writable": writable,
            "description": description,
        })
        previous_end = end

    if partitions[0]["offset"] != 0:
        raise ValueError("partitions: first partition must start at zero")
    if previous_end != flash["maximum_capacity"]:
        raise ValueError("partitions: map must cover the maximum capacity without a tail gap")
    return flash, partitions


def load_validated(
    path: Path,
) -> tuple[dict[str, int], list[dict[str, Any]]]:
    """Load and validate the canonical map for build/release/flasher tools."""

    return validate(load_layout(path))


def partition_by_name(
    partitions: list[dict[str, Any]], name: str
) -> dict[str, Any]:
    for partition in partitions:
        if partition["name"] == name:
            return partition
    raise ValueError(f"partitions: required partition {name!r} is missing")


def macro_name(name: str) -> str:
    return name.upper()


def render(source: Path, flash: dict[str, int], partitions: list[dict[str, Any]]) -> str:
    lines = [
        "#ifndef HK_FLASH_LAYOUT_H",
        "#define HK_FLASH_LAYOUT_H",
        "",
        f"/* Generated by tools/gen_flash_layout.py from {source.as_posix()}.",
        " * Do not edit this file without updating the canonical JSON map. */",
        "",
        f"#define HK_FLASH_ADDRESS_BYTES {flash['address_bytes']}U",
        f"#define HK_FLASH_MIN_CAPACITY 0x{flash['minimum_capacity']:08X}UL",
        f"#define HK_FLASH_MAX_CAPACITY 0x{flash['maximum_capacity']:08X}UL",
        f"#define HK_FLASH_ERASE_SIZE 0x{flash['erase_size']:08X}UL",
        f"#define HK_FLASH_PROGRAM_SIZE 0x{flash['program_size']:08X}UL",
        "",
        "typedef enum",
        "{",
    ]
    for index, part in enumerate(partitions):
        lines.append(f"    HK_FLASH_PARTITION_{macro_name(part['name'])} = {index},")
    lines.extend([
        f"    HK_FLASH_PARTITION_COUNT = {len(partitions)}",
        "} hk_flash_partition_id_t;",
        "",
    ])
    for part in partitions:
        name = macro_name(part["name"])
        lines.extend([
            f"#define HK_FLASH_PARTITION_{name}_OFFSET 0x{part['offset']:08X}UL",
            f"#define HK_FLASH_PARTITION_{name}_SIZE 0x{part['size']:08X}UL",
            f"#define HK_FLASH_PARTITION_{name}_REQUIRED_CAPACITY 0x{part['required_capacity']:08X}UL",
            f"#define HK_FLASH_PARTITION_{name}_RUNTIME_WRITABLE {1 if part['runtime_writable'] else 0}U",
            "",
        ])
    lines.extend(["#endif", ""])
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board", required=True,
                        help="Canonical boards/<id>/board.toml ID")
    parser.add_argument("--check", action="store_true",
                        help="fail if the generated header is absent or stale")
    args = parser.parse_args()
    try:
        from board_contract import load_board
        board = load_board(args.board)
    except (ImportError, ValueError) as exc:
        print(f"board contract error: {exc}", file=sys.stderr)
        return 2
    input_path = board.flash_layout_path
    output_path = board.generated_dir / "flash_layout.h"
    try:
        flash, partitions = load_validated(input_path)
        generated = render(input_path.relative_to(ROOT), flash, partitions)
    except ValueError as exc:
        print(f"flash layout error: {exc}", file=sys.stderr)
        return 2

    if args.check:
        try:
            current = output_path.read_text(encoding="utf-8")
        except OSError as exc:
            print(f"flash layout header unavailable: {exc}", file=sys.stderr)
            return 1
        if current != generated:
            print(f"flash layout header is stale: {output_path}", file=sys.stderr)
            return 1
        print(f"flash layout valid: {len(partitions)} partitions, "
              f"0x{flash['maximum_capacity']:08X} bytes")
        return 0

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(generated, encoding="utf-8", newline="\n")
    print(f"generated {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
