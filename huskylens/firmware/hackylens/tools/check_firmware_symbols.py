#!/usr/bin/env python3
"""Verify that a firmware ELF includes or excludes the MicroPython feature."""

from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import build_firmware


SYMBOL_GROUPS = {
    "micropython-core": ("mp_",),
    "runtime": ("micropython_",),
    "protocol": ("hmpy_",),
    "storage": ("userfs_",),
}

REQUIRED_PRESENT_SYMBOLS = {
    "mp_builtin_max_obj",
    "mp_builtin_min_obj",
    "mp_builtin_sum_obj",
}


def symbol_groups(symbols: set[str]) -> dict[str, list[str]]:
    return {
        group: sorted(
            symbol for symbol in symbols
            if any(symbol.startswith(prefix) for prefix in prefixes)
        )
        for group, prefixes in SYMBOL_GROUPS.items()
    }


def verify(symbols: set[str], expectation: str) -> dict[str, list[str]]:
    groups = symbol_groups(symbols)
    if expectation == "present":
        missing = [group for group, matches in groups.items() if not matches]
        if missing:
            raise ValueError("missing symbol groups: " + ", ".join(missing))
        missing_symbols = sorted(REQUIRED_PRESENT_SYMBOLS - symbols)
        if missing_symbols:
            raise ValueError("missing required symbols: " + ", ".join(missing_symbols))
    elif expectation == "absent":
        leaked = {
            group: matches[:5] for group, matches in groups.items() if matches
        }
        if leaked:
            detail = "; ".join(
                f"{group}={','.join(matches)}" for group, matches in leaked.items()
            )
            raise ValueError("disabled feature leaked symbols: " + detail)
    else:
        raise ValueError(f"unknown expectation {expectation!r}")
    return groups


def find_nm() -> Path:
    toolchain = build_firmware.find_toolchain_bin()
    if toolchain:
        for name in ("riscv64-unknown-elf-nm.exe", "riscv64-unknown-elf-nm"):
            candidate = toolchain / name
            if candidate.is_file():
                return candidate
    for name in ("riscv64-unknown-elf-nm", "riscv64-unknown-elf-nm.exe"):
        candidate = shutil.which(name)
        if candidate:
            return Path(candidate)
    raise RuntimeError("riscv64-unknown-elf-nm not found")


def read_symbols(elf: Path) -> set[str]:
    result = subprocess.run(
        [str(find_nm()), "--defined-only", str(elf)],
        check=True,
        text=True,
        capture_output=True,
    )
    symbols: set[str] = set()
    for line in result.stdout.splitlines():
        fields = line.split()
        if fields:
            symbols.add(fields[-1])
    return symbols


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("--expect", choices=("present", "absent"), required=True)
    args = parser.parse_args()
    if not args.elf.is_file():
        print(f"firmware ELF not found: {args.elf}", file=sys.stderr)
        return 2
    try:
        groups = verify(read_symbols(args.elf), args.expect)
    except (OSError, subprocess.CalledProcessError, RuntimeError, ValueError) as exc:
        print(f"firmware symbol check failed: {exc}", file=sys.stderr)
        return 1
    counts = " ".join(f"{group}={len(matches)}" for group, matches in groups.items())
    print(f"firmware symbols {args.expect}: {counts}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
