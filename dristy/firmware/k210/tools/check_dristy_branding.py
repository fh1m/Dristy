#!/usr/bin/env python3
"""Reject product-facing legacy Dristy/Dristy branding outside allowlists."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parents[2]

PATTERN = re.compile(r"Dristy|Dristy|SEN0305|dristy|dristy", re.I)

SKIP_DIR_NAMES = {
    "_deps",
    "build",
    "dist",
    ".git",
    "node_modules",
    "third_party",
    "dristy.kmodels",
    "dristy.kmodels",
}

SKIP_FILE_SUFFIXES = {".bin", ".kmodel", ".obj", ".o", ".a", ".elf"}

ALLOWLIST_SUBSTR = (
    "check_dristy_branding.py",
    "UPSTREAM.txt",
    "attestation.json",
    "RESEARCH_SEN0305",
    "GATE0",
    "GATE1",
    "GATE2",
    "DRISTY_CAPABILITY",
    "path-map",
    "dristy-path-map",
    "sen0305",
    "dristy_full",
    "dristy-full",
)


def should_scan(path: Path) -> bool:
    if any(part in SKIP_DIR_NAMES for part in path.parts):
        return False
    if path.suffix.lower() in SKIP_FILE_SUFFIXES:
        return False
    if path.is_dir():
        return False
    rel = str(path)
    if any(token in rel for token in ALLOWLIST_SUBSTR):
        return False
    if "docs/research" in rel or "docs/evidence" in rel:
        return False
    return True


def scan_roots() -> list[tuple[Path, int, str]]:
    hits: list[tuple[Path, int, str]] = []
    roots = [
        ROOT / "firmware" / "src",
        ROOT / "tools",
        WORKSPACE / "dristy-py",
        WORKSPACE / "tools",
    ]
    for base in roots:
        if not base.is_dir():
            continue
        for path in base.rglob("*"):
            if not should_scan(path):
                continue
            try:
                text = path.read_text(encoding="utf-8", errors="ignore")
            except OSError:
                continue
            for i, line in enumerate(text.splitlines(), start=1):
                if PATTERN.search(line):
                    hits.append((path, i, line.strip()[:120]))
    return hits


def main() -> int:
    hits = scan_roots()
    if hits:
        print(f"[ERR] {len(hits)} legacy branding hit(s):", file=sys.stderr)
        for path, line_no, snippet in hits[:40]:
            print(f"  {path}:{line_no}: {snippet}", file=sys.stderr)
        if len(hits) > 40:
            print(f"  … and {len(hits) - 40} more", file=sys.stderr)
        return 1
    print("[OK] product-facing branding check passed (allowlisted paths excluded)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
