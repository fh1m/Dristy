#!/usr/bin/env python3
"""Rapid DLP mode cycling + serial fault scan (CAMERA FAIL / NO FRAME)."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

DEBUG_LOG = Path(
    "/home/fh1m/Envs/dockers/auv-ros2/Ros_workspaces/R_n_d-ws/.cursor/debug-6876a9.log"
)

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


def _agent_log(message: str, data: dict) -> None:
    payload = {
        "sessionId": "6876a9",
        "runId": data.pop("runId", "stress"),
        "hypothesisId": "STRESS",
        "location": "dristy_mode_stress.py",
        "message": message,
        "data": data,
        "timestamp": int(time.time() * 1000),
    }
    try:
        with DEBUG_LOG.open("a", encoding="utf-8") as fh:
            fh.write(json.dumps(payload, separators=(",", ":")) + "\n")
    except OSError:
        pass


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
    parser.add_argument("--run-id", default="stress")
    args = parser.parse_args()

    _agent_log("stress start", {"runId": args.run_id, "port": args.port})

    faults: list[str] = []
    with Dristy(args.port, timeout=2.5) as cam:
        cam.connect()
        cam.lcd_on()
        for cycle in range(args.cycles):
            for mode in STRESS_MODES:
                cam.set_mode(mode)
                time.sleep(0.35 if mode not in (Mode.FACE_DETECT, Mode.DETECT_TRACK) else 0.55)
                ok = cam.get_mode() == mode
                _agent_log(
                    "cycle mode",
                    {
                        "runId": args.run_id,
                        "cycle": cycle,
                        "mode": mode.name,
                        "mode_ok": ok,
                    },
                )
                if not ok:
                    faults.append(f"mode mismatch {mode.name} cycle={cycle}")

    log = serial_excerpt(args.port, 8.0)
    for needle in ("CAMERA FAIL", "NO FRAME", "VISION FAIL"):
        if needle in log:
            faults.append(f"serial contains {needle!r}")

    _agent_log(
        "stress done",
        {"runId": args.run_id, "faults": faults, "log_tail": log[-1500:]},
    )

    if faults:
        print(json.dumps({"faults": faults}, indent=2))
        return 1
    print(json.dumps({"ok": True, "cycles": args.cycles}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
