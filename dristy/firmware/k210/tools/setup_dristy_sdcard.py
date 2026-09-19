#!/usr/bin/env python3
"""Stage a FAT32 SD card image tree for Dristy (models + legacy mirror)."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STAGING = ROOT / "sdcard"
DRISTY_ROOT = STAGING / "dristy.kmodels"
LEGACY_ROOT = STAGING / "dristy.kmodels"
OBJECT_SPEC = ROOT / "models" / "object_detect_voc20.json"
AI_MODEL = ROOT / "tools" / "ai_model.py"


def copy_tree(src: Path, dst: Path) -> None:
    if not src.is_dir():
        raise SystemExit(f"missing staging directory: {src}")
    dst.mkdir(parents=True, exist_ok=True)
    for item in src.iterdir():
        target = dst / item.name
        if item.is_dir():
            if target.exists():
                shutil.rmtree(target)
            shutil.copytree(item, target)
        else:
            shutil.copy2(item, target)


def ensure_face_model(dristy: Path, legacy: Path) -> None:
    face = dristy / "detect.kmodel"
    if face.is_file():
        return
    legacy_face = legacy / "detect.kmodel"
    if legacy_face.is_file():
        shutil.copy2(legacy_face, face)
        shutil.copy2(legacy / "detect.UPSTREAM.txt", dristy / "detect.UPSTREAM.txt")
        return
    raise SystemExit(
        "face detect.kmodel missing; run ai_model fetch or copy from Kendryte face_detect demo"
    )


def fetch_object20(out_dir: Path) -> None:
    if (out_dir / "model.kmodel").is_file():
        return
    cmd = [
        sys.executable,
        str(AI_MODEL),
        "fetch",
        "--spec",
        str(OBJECT_SPEC),
        "--out-dir",
        str(out_dir),
    ]
    print("+", " ".join(cmd))
    subprocess.run(cmd, check=True, cwd=ROOT)


def main() -> int:
    parser = argparse.ArgumentParser(description="Prepare Dristy SD card contents")
    parser.add_argument(
        "--mount",
        type=Path,
        help="Copy staged tree to mounted SD root (FAT32). Omit to only refresh sdcard/",
    )
    parser.add_argument("--fetch", action="store_true", help="Run ai_model fetch if object20 missing")
    args = parser.parse_args()

    if args.fetch:
        fetch_object20(DRISTY_ROOT / "object20")

    ensure_face_model(DRISTY_ROOT, LEGACY_ROOT)
    copy_tree(DRISTY_ROOT / "object20", LEGACY_ROOT / "object20")
    if (DRISTY_ROOT / "detect.kmodel").is_file():
        shutil.copy2(DRISTY_ROOT / "detect.kmodel", LEGACY_ROOT / "detect.kmodel")

    print(f"[OK] staged under {STAGING}")
    print("Primary paths on card:")
    print("  /dristy.kmodels/detect.kmodel")
    print("  /dristy.kmodels/object20/model.kmodel")
    print("  /dristy.kmodels/object20/manifest.hkai")
    print("  /dristy.kmodels/object20/labels.txt")
    print("Legacy mirror (optional): /dristy.kmodels/...")

    if args.mount:
        if not args.mount.is_dir():
            raise SystemExit(f"mount path not found: {args.mount}")
        for name in ("dristy.kmodels", "dristy.kmodels"):
            copy_tree(STAGING / name, args.mount / name)
        print(f"[OK] copied to {args.mount}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
