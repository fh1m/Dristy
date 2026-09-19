#!/usr/bin/env python3
"""Per-mode DLP smoke over serial (all catalog modes)."""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DRISTY_PY = ROOT.parent.parent / "dristy-py"
sys.path.insert(0, str(DRISTY_PY))

from dristy import Dristy  # noqa: E402
from dristy.types import Mode  # noqa: E402

LIVE_MODES = list(Mode)


def smoke_mode(cam: Dristy, mode: Mode) -> dict:
    cam.set_mode(mode)
    if mode in (Mode.FACE_DETECT, Mode.FACE_RECOGNISE, Mode.DETECT, Mode.DETECT_TRACK):
        time.sleep(1.0)
    elif mode in (Mode.DETECT_CUSTOM, Mode.CLASSIFY):
        time.sleep(1.4)
    else:
        time.sleep(0.65)
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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/dev/ttyUSB0")
    parser.add_argument("--out", type=Path, default=None)
    parser.add_argument(
        "--host-parity",
        action="store_true",
        help="After mode matrix, append classical blob/flow checks for selected modes",
    )
    args = parser.parse_args()

    results = []
    with Dristy(args.port, timeout=2.5) as cam:
        cam.connect()
        cam.lcd_on()
        time.sleep(0.3)
        for mode in LIVE_MODES:
            try:
                results.append(smoke_mode(cam, mode))
            except Exception as exc:  # noqa: BLE001
                results.append({"mode": mode.name, "error": str(exc)})
        if args.host_parity:
            for mode in (Mode.COLOUR_TRACK, Mode.OPTICAL_FLOW, Mode.DETECT_TRACK):
                cam.lcd_on()
                cam.set_mode(mode)
                time.sleep(1.2)
                results.append(
                    {
                        "host_parity": {
                            "mode": mode.name,
                            "mode_ok": cam.get_mode() == mode,
                            "blobs": len(cam.read_blobs()),
                            "flow": len(cam.read_flow()),
                            "detections": cam.read().detection_count,
                        }
                    }
                )

    payload = {"modes": results, "ts": time.time()}
    text = json.dumps(payload, indent=2)
    print(text)
    if args.out:
        args.out.write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
