#!/usr/bin/env python3
"""Probe Dristy DLP on USB debug UART or Gravity link."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "dristy-py"))

from dristy.core import Dristy, DristyError, DristyTimeout  # noqa: E402
from dristy.protocol import CMD_IDENTIFY, build_packet, parse_packet  # noqa: E402


def try_stock_knock(port: str, baud: int) -> bool:
    try:
        import serial
    except ImportError:
        return False
    pkt = build_packet(0x2C, b"")  # Husky KNOCK
    try:
        with serial.Serial(port, baud, timeout=0.5) as ser:
            ser.reset_input_buffer()
            ser.write(pkt)
            buf = bytearray()
            deadline = __import__("time").monotonic() + 1.0
            while __import__("time").monotonic() < deadline:
                chunk = ser.read(64)
                if chunk:
                    buf.extend(chunk)
                    if parse_packet(buf):
                        return True
    except OSError:
        return False
    return False


def main() -> int:
    parser = argparse.ArgumentParser(description="Dristy device doctor")
    parser.add_argument("--port", default="/dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()

    print(f"[doctor] port={args.port} baud={args.baud}")
    try:
        cam = Dristy(args.port, baud=args.baud, timeout=2.0)
        identity = cam.connect()
        print(f"[OK] DLP identify: {identity!r}")
        mode = cam.get_mode()
        print(f"[OK] mode={mode}")
        cam.lcd_off()
        print("[OK] lcd_off")
        cam.lcd_on(overlay=True)
        print("[OK] lcd_on overlay")
        perf = cam.get_perf()
        print(f"[OK] perf={perf}")
        cam.close()
    except (DristyTimeout, DristyError, OSError) as exc:
        print(f"[WARN] DLP path failed: {exc}")
        if try_stock_knock(args.port, args.baud):
            print("[OK] stock Dristy KNOCK responded (not Dristy firmware)")
            return 2
        print("[ERR] no Dristy or stock response")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
