#!/usr/bin/env python3
"""Rapid DLP mode cycling + serial fault scan (CAMERA FAIL / NO FRAME)."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HACKYLENS = ROOT
HUSKY = ROOT.parents[1]
DRISTY_PY = HUSKY / "dristy-py"
HKFLASH = ROOT / "tools" / "hkflash.py"

sys.path.insert(0, str(DRISTY_PY))

from dristy import Dristy  # noqa: E402
from dristy.types import Mode  # noqa: E402

STRESS_MODES = [
    Mode.FACE_DETECT,
    Mode.COLOUR_TRACK,
    Mode.OPTICAL_FLOW,
    Mode.DETECT_TRACK,
    Mode.APRILTAG,
    Mode.LINE_FOLLOW,
    Mode.QR_CODE,
]


def serial_excerpt(port: str, duration: float) -> str:
    proc = subprocess.run(
        [
            "sudo",
            "python3",
            str(HKFLASH),
            "monitor",
            "--board",
            "huskylens-sen0305",
            "--port",
            port,
            "--duration",
            str(duration),
            "--reset-before-read",
        ],
        cwd=HACKYLENS,
        text=True,
        capture_output=True,
        timeout=duration + 30,
        check=False,
    )
    return proc.stdout + proc.stderr


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/dev/ttyUSB0")
    parser.add_argument("--cycles", type=int, default=2)
    args = parser.parse_args()

    faults: list[str] = []
    with Dristy(args.port, timeout=2.5) as cam:
        cam.connect()
        cam.lcd_on()
        for cycle in range(args.cycles):
            for mode in STRESS_MODES:
                cam.set_mode(mode)
                time.sleep(0.35 if mode not in (Mode.FACE_DETECT, Mode.DETECT_TRACK) else 0.55)
                if cam.get_mode() != mode:
                    faults.append(f"mode mismatch {mode.name} cycle={cycle}")

    log = serial_excerpt(args.port, 8.0)
    for needle in ("CAMERA FAIL", "NO FRAME", "VISION FAIL"):
        if needle in log:
            faults.append(f"serial contains {needle!r}")

    if faults:
        print(json.dumps({"faults": faults}, indent=2))
        return 1
    print(json.dumps({"ok": True, "cycles": args.cycles}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
