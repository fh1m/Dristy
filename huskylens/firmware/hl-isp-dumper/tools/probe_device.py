#!/usr/bin/env python3
"""Load an ISP stub into K210 SRAM and test D2 without touching flash."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys
import time


ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parent
sys.path.insert(0, str(WORKSPACE / "hackylens" / "tools"))

import hkflash  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("stub", type=Path)
    args = parser.parse_args()
    serial = hkflash.require_serial()
    with serial.Serial(args.port, hkflash.DEFAULT_BOOT_BAUD, timeout=0.1) as ser:
        isp = hkflash.K210Isp(ser)
        hkflash.connect_bootrom_uploader(isp, ser, attempts=15)
        data = args.stub.read_bytes()
        print(f"[PROBE] loading {len(data)} bytes")
        isp.memory_write(hkflash.STUB_ADDRESS, data)
        isp.boot_sram(hkflash.STUB_ADDRESS)
        time.sleep(0.3)
        isp.flash_greeting()
        print("[OK] D2/E0 greeting received; flash was not accessed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
