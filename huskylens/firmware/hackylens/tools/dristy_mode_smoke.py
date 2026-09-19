#!/usr/bin/env python3
"""Per-mode DLP smoke over serial (all catalog modes)."""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

DEBUG_LOG = Path(
    "/home/fh1m/Envs/dockers/auv-ros2/Ros_workspaces/R_n_d-ws/.cursor/debug-6876a9.log"
)


def _agent_log(hypothesis_id: str, location: str, message: str, data: dict) -> None:
    # #region agent log
    payload = {
        "sessionId": "6876a9",
        "runId": data.pop("runId", "smoke"),
        "hypothesisId": hypothesis_id,
        "location": location,
        "message": message,
        "data": data,
        "timestamp": int(time.time() * 1000),
    }
    try:
        with DEBUG_LOG.open("a", encoding="utf-8") as fh:
            fh.write(json.dumps(payload, separators=(",", ":")) + "\n")
    except OSError:
        pass
    # #endregion


ROOT = Path(__file__).resolve().parents[1]
DRISTY_PY = ROOT.parent.parent / "dristy-py"
sys.path.insert(0, str(DRISTY_PY))

from dristy import Dristy  # noqa: E402
from dristy.types import Mode  # noqa: E402

LIVE_MODES = list(Mode)


def smoke_mode(cam: Dristy, mode: Mode) -> dict:
    cam.set_mode(mode)
    if mode in (Mode.FACE_DETECT, Mode.FACE_RECOGNISE, Mode.DETECT, Mode.DETECT_TRACK):
        time.sleep(0.8)
    else:
        time.sleep(0.45)
    got = cam.get_mode()
    frame = cam.read()
    tags = cam.read_tags()
    tracks = cam.read_tracks()
    blobs = cam.read_blobs()
    qr = cam.read_qr()
    flow = cam.read_flow()
    return {
        "mode": mode.name,
        "mode_ok": got == mode,
        "detections": frame.detection_count if hasattr(frame, "detection_count") else 0,
        "tracks": len(tracks),
        "tags": len(tags),
        "blobs": len(blobs),
        "qr": len(qr),
        "flow": len(flow),
    }


def smoke_mode_logged(cam: Dristy, mode: Mode) -> dict:
    row = smoke_mode(cam, mode)
    _agent_log(
        "API",
        "dristy_mode_smoke.py:smoke_mode",
        "mode smoke row",
        {"runId": "smoke", **row},
    )
    if not row.get("mode_ok"):
        _agent_log(
            "API",
            "dristy_mode_smoke.py:smoke_mode",
            "SET_MODE mismatch",
            {"runId": "smoke", "mode": row["mode"]},
        )
    return row


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/dev/ttyUSB0")
    parser.add_argument("--out", type=Path, default=None)
    parser.add_argument("--run-id", default="smoke", help="Debug log runId tag")
    parser.add_argument(
        "--host-parity",
        action="store_true",
        help="After mode matrix, log classical blob/flow counts after host SET_MODE",
    )
    args = parser.parse_args()

    _agent_log("API", "dristy_mode_smoke.py:main", "smoke start", {"runId": args.run_id, "port": args.port})

    results = []
    try:
        with Dristy(args.port) as cam:
            cam.connect()
            _agent_log("API", "dristy_mode_smoke.py:main", "connected", {"runId": args.run_id})
            for mode in LIVE_MODES:
                try:
                    results.append(smoke_mode_logged(cam, mode))
                except Exception as exc:  # noqa: BLE001
                    row = {"mode": mode.name, "error": str(exc)}
                    results.append(row)
                    _agent_log(
                        "API",
                        "dristy_mode_smoke.py:main",
                        "mode exception",
                        {"runId": args.run_id, **row},
                    )
            if args.host_parity:
                for mode in (Mode.COLOUR_TRACK, Mode.OPTICAL_FLOW, Mode.DETECT_TRACK):
                    cam.lcd_on()
                    cam.set_mode(mode)
                    time.sleep(1.2)
                    row = {
                        "runId": args.run_id,
                        "mode": mode.name,
                        "mode_ok": cam.get_mode() == mode,
                        "blobs": len(cam.read_blobs()),
                        "flow": len(cam.read_flow()),
                        "detections": cam.read().detection_count,
                    }
                    _agent_log(
                        "HOST",
                        "dristy_mode_smoke.py:host_parity",
                        "host set_mode classical/kpu settle",
                        row,
                    )
                    results.append({"host_parity": row})
    except Exception as exc:  # noqa: BLE001
        _agent_log(
            "API",
            "dristy_mode_smoke.py:main",
            "connect failed",
            {"runId": args.run_id, "error": str(exc)},
        )
        raise

    payload = {"modes": results, "ts": time.time()}
    text = json.dumps(payload, indent=2)
    print(text)
    if args.out:
        args.out.write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
