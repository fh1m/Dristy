#!/usr/bin/env python3
"""Full DLP + menu-path audit: every Mode, GET_RESULT counts, dedicated reads."""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT.parent.parent / "dristy-py"))

from dristy import Dristy  # noqa: E402
from dristy.types import Mode  # noqa: E402

SETTLE = {
    Mode.FACE_DETECT: 1.2,
    Mode.FACE_RECOGNISE: 1.2,
    Mode.DETECT: 1.0,
    Mode.DETECT_TRACK: 1.0,
    Mode.DETECT_CUSTOM: 1.4,
    Mode.CLASSIFY: 1.4,
    Mode.DETECT_TAG: 1.2,
    Mode.DETECT_MOTION: 1.2,
    Mode.DETECT_ARUCO: 1.2,
}


def audit_mode(cam: Dristy, mode: Mode) -> dict:
    row: dict = {"mode": mode.name, "id": int(mode)}
    try:
        cam.set_mode(mode)
        time.sleep(SETTLE.get(mode, 0.8))
        got = cam.get_mode()
        row["mode_ok"] = got == mode
        if got != mode:
            row["got"] = getattr(got, "name", str(got))
        frame = cam.read()
        row["fps"] = round(float(frame.fps), 1)
        row["n_det"] = int(frame.n_detections)
        row["n_trk"] = int(frame.n_tracks)
        row["n_tag"] = int(frame.n_tags)
        row["n_blob"] = int(frame.n_blobs)
        row["n_flow"] = int(frame.n_flow)
        row["n_qr"] = int(frame.n_qr)
        row["line"] = bool(frame.line_valid)
        row["motion_pct"] = round(float(frame.motion_percent), 2)
        row["tracks"] = len(cam.read_tracks())
        row["tags"] = len(cam.read_tags())
        row["blobs"] = len(cam.read_blobs())
        row["flow"] = len(cam.read_flow())
        row["qr"] = len(cam.read_qr())
        perf = cam.get_perf()
        row["perf_fps"] = perf.get("fps")
        row["frames"] = perf.get("frame_count")
    except Exception as exc:  # noqa: BLE001
        row["error"] = str(exc)
    return row


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/dev/ttyUSB0")
    parser.add_argument("--out", type=Path, default=None)
    args = parser.parse_args()

    rows = []
    with Dristy(args.port, timeout=2.5) as cam:
        cam.connect()
        cam.lcd_on()
        time.sleep(0.4)
        for mode in Mode:
            rows.append(audit_mode(cam, mode))

    payload = {"ts": time.time(), "modes": rows}
    text = json.dumps(payload, indent=2)
    print(text)
    if args.out:
        args.out.write_text(text, encoding="utf-8")

    errors = [r for r in rows if r.get("error") or not r.get("mode_ok")]
    print(
        f"[audit] {len(rows) - len(errors)}/{len(rows)} mode_ok, "
        f"{len(errors)} fail",
        file=sys.stderr,
    )
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
