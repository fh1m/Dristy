"""
OpenCV host viewer for Dristy — processing stays on device; host draws overlays.
"""

from __future__ import annotations

import argparse
import sys
import time
from typing import List, Optional, Tuple

try:
    import cv2
    import numpy as np
except ImportError:
    print("Install opencv: pip install opencv-python numpy", file=sys.stderr)
    raise

from dristy import Dristy, Mode
from dristy.types import Frame, Target, Track, Tag, Blob


W, H = 320, 240


def norm_to_px(nx: int, ny: int) -> Tuple[int, int]:
    px = int((nx + 1000) * W / 2000)
    py = int((ny + 1000) * H / 2000)
    return max(0, min(W - 1, px)), max(0, min(H - 1, py))


def draw_target(img: np.ndarray, target: Target) -> None:
    if not target.valid:
        return
    cx, cy = norm_to_px(target.error_x, target.error_y)
    cv2.drawMarker(img, (cx, cy), (0, 120, 255), cv2.MARKER_CROSS, 16, 2)


def draw_tracks(img: np.ndarray, tracks: List[Track]) -> None:
    for t in tracks:
        cx, cy = norm_to_px(t.cx, t.cy)
        cv2.rectangle(img, (cx - 20, cy - 15), (cx + 20, cy + 15), (255, 80, 0), 1)
        cv2.putText(img, str(t.id), (cx + 4, cy - 4), cv2.FONT_HERSHEY_SIMPLEX, 0.35, (200, 200, 200), 1)


def draw_tags(img: np.ndarray, tags: List[Tag]) -> None:
    for tag in tags:
        cx, cy = norm_to_px(tag.cx, tag.cy)
        cv2.rectangle(img, (cx - 12, cy - 12), (cx + 12, cy + 12), (0, 255, 120), 1)
        cv2.putText(img, f"id{tag.tag_id}", (cx + 4, cy + 12), cv2.FONT_HERSHEY_SIMPLEX, 0.35, (0, 255, 120), 1)


def draw_blobs(img: np.ndarray, blobs: List[Blob]) -> None:
    for b in blobs:
        cx, cy = norm_to_px(b.cx, b.cy)
        cv2.circle(img, (cx, cy), 8, (255, 0, 200), 1)


def draw_hud(img: np.ndarray, frame: Frame, mode_name: str) -> None:
    lines = [
        f"mode={mode_name} fps={frame.fps:.1f}",
        f"det={len(tracks)} trk={len(tracks)} tag={len(tags)}",
    ]
    if frame.target.valid:
        lines.append(f"target ex={frame.target.error_x} ey={frame.target.error_y}")
    y = 16
    for line in lines:
        cv2.putText(img, line, (8, y), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (220, 220, 220), 1)
        y += 16


def mode_cycle(m: Mode, step: int) -> Mode:
    modes = list(Mode)
    try:
        i = modes.index(m)
    except ValueError:
        return Mode.DETECT_TRACK
    return modes[(i + step) % len(modes)]


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Dristy OpenCV host viewer")
    parser.add_argument("--port", default="/dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--rate", type=float, default=20.0)
    parser.add_argument("--mode", default="detect_track")
    parser.add_argument("--headless-lcd", action="store_true", help="Turn off device LCD")
    args = parser.parse_args(argv)

    mode = Mode.from_name(args.mode)
    interval = 1.0 / max(1.0, args.rate)

    cam = Dristy(args.port, baud=args.baud, timeout=2.0)
    try:
        cam.connect()
        cam.set_mode(mode)
        if args.headless_lcd:
            cam.lcd_off()
    except Exception as exc:
        print(f"Connect failed: {exc}", file=sys.stderr)
        return 1

    win = "Dristy viewer (q quit, [/] mode, s snapshot)"
    cv2.namedWindow(win, cv2.WINDOW_NORMAL)
    cv2.resizeWindow(win, W * 2, H * 2)

    try:
        while True:
            t0 = time.monotonic()
            canvas = np.zeros((H, W, 3), dtype=np.uint8)
            try:
                frame = cam.read()
                tracks = cam.read_tracks()
                tags = cam.read_tags()
                blobs = cam.read_blobs()
            except Exception:
                frame = Frame()
                tracks, tags, blobs = [], [], []

            draw_tracks(canvas, tracks)
            draw_tags(canvas, tags)
            draw_blobs(canvas, blobs)
            draw_target(canvas, frame.target)
            draw_hud(canvas, frame, mode.name.lower())

            cv2.imshow(win, canvas)
            key = cv2.waitKey(1) & 0xFF
            if key == ord("q"):
                break
            if key == ord("["):
                mode = mode_cycle(mode, -1)
                cam.set_mode(mode)
            if key == ord("]"):
                mode = mode_cycle(mode, 1)
                cam.set_mode(mode)
            if key == ord("s"):
                path = f"dristy_view_{int(time.time())}.png"
                cv2.imwrite(path, canvas)
                print(f"saved {path}")

            elapsed = time.monotonic() - t0
            if elapsed < interval:
                time.sleep(interval - elapsed)
    finally:
        cam.close()
        cv2.destroyAllWindows()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
