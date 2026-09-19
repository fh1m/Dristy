#!/usr/bin/env python3
"""Fail if dist firmware is older than firmware sources (stale image trap)."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "firmware" / "src"
DIST = ROOT / "dist" / "hackylens-full-huskylens-sen0305.bin"


def newest_source_mtime() -> float:
    latest = 0.0
    for path in SRC.rglob("*"):
        if path.suffix in {".c", ".h"} and path.is_file():
            latest = max(latest, path.stat().st_mtime)
    return latest


def main() -> int:
    if not DIST.is_file():
        print(f"[ERR] missing dist image: {DIST}", file=sys.stderr)
        return 1
    src_t = newest_source_mtime()
    dist_t = DIST.stat().st_mtime
    if src_t > dist_t + 1.0:
        print(
            f"[ERR] stale dist: image mtime older than newest firmware/src "
            f"({dist_t:.0f} < {src_t:.0f}); rebuild with build_firmware.py",
            file=sys.stderr,
        )
        return 1
    print(f"[OK] dist fresh enough (dist={dist_t:.0f} src={src_t:.0f})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
