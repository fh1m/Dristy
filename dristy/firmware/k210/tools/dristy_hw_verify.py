#!/usr/bin/env python3
"""Build, flash, and verify Dristy firmware on SEN0305 (/dev/ttyUSB0).

Serial access: add your user to the ``dialout`` group (or a udev rule for CP210x)
so ``/dev/ttyUSB0`` is readable without sudo for doctor/monitor steps.
Flash still typically requires ``sudo python tools/hkflash.py ...``.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
import time
from pathlib import Path

DRISTY = Path(__file__).resolve().parents[1]
HUSKY = DRISTY.parents[1]
IMAGE = DRISTY / "dist" / "dristy-full-sen0305.bin"
DOCTOR = HUSKY / "tools" / "dristy_doctor.py"
HKFLASH = DRISTY / "tools" / "hkflash.py"
ENV_SH = DRISTY / "env.sh"


def run(cmd: list[str], cwd: Path, timeout: float | None = None) -> subprocess.CompletedProcess:
    print("+", " ".join(cmd), flush=True)
    return subprocess.run(
        cmd, cwd=cwd, text=True, capture_output=True, timeout=timeout, check=False,
    )


def bmp_is_dristy(path: Path) -> dict:
    data = path.read_bytes()
    if len(data) < 54:
        return {"ok": False, "reason": "short bmp"}
    off = int.from_bytes(data[10:14], "little")
    w, h = int.from_bytes(data[18:22], "little"), int.from_bytes(data[22:26], "little")
    row = w * 3
    pad = (4 - (row % 4)) % 4
    green = 0
    for y in range(h):
        base = off + y * (row + pad)
        for x in range(w):
            b, g, r = data[base + x * 3 : base + x * 3 + 3]
            if g > 200 and r < 80 and b < 80:
                green += 1
    return {"ok": green < 100, "green_pixels": green, "width": w, "height": h}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/dev/ttyUSB0")
    parser.add_argument("--skip-flash", action="store_true")
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()

    failures: list[str] = []

    if not args.skip_build:
        build = run(
            ["bash", "-lc", f"source {ENV_SH} && python tools/build_firmware.py full --board sen0305"],
            DRISTY,
            timeout=600,
        )
        if build.returncode != 0:
            print(build.stdout[-4000:], file=sys.stderr)
            print(build.stderr[-4000:], file=sys.stderr)
            failures.append("build")
        elif not IMAGE.is_file():
            failures.append("missing dist image")

    if not IMAGE.is_file():
        print("[ERR] no firmware image", file=sys.stderr)
        return 1

    sha = hashlib.sha256(IMAGE.read_bytes()).hexdigest()
    print(f"[OK] image sha256={sha[:16]}… size={IMAGE.stat().st_size}")

    if not args.skip_flash:
        flash = run(
            ["sudo", "python", str(HKFLASH), "flash", str(IMAGE),
             "--board", "sen0305", "--port", args.port],
            DRISTY,
            timeout=300,
        )
        if flash.returncode != 0 or "[OK] flash complete" not in flash.stdout:
            print(flash.stdout, flash.stderr, file=sys.stderr)
            failures.append("flash")
        else:
            print("[OK] flash complete")

    mon = run(
        ["sudo", "python", str(HKFLASH), "monitor", "--board", "sen0305",
         "--port", args.port, "--duration", "15", "--reset-before-read"],
        DRISTY,
        timeout=45,
    )
    log = mon.stdout + mon.stderr
    if mon.returncode != 0:
        failures.append("serial monitor")
    for needle in ("[BOOT] Dristy", "[BTN] menu: L/R=prev/next", "[SHELL] screen MENU"):
        if needle not in log:
            failures.append(f"serial missing {needle!r}")
    if not failures:
        print("[OK] boot serial markers")

    time.sleep(1.5)
    shot_path = Path("/tmp/dristy_hw_verify_menu.bmp")
    shot = run(
        ["sudo", "python", str(HKFLASH), "screenshot", "--board", "sen0305",
         "--port", args.port, "--reset-before-read", "--reset-wait", "4.5",
         "--timeout", "60", "-o", str(shot_path)],
        DRISTY,
        timeout=120,
    )
    if shot.returncode == 0 and shot_path.is_file():
        bmp = bmp_is_dristy(shot_path)
        print(f"[OK] screenshot {bmp}")
        if not bmp.get("ok"):
            failures.append("menu still terminal-green")
    else:
        failures.append("HKSHOT")

    time.sleep(1.5)
    if DOCTOR.is_file():
        time.sleep(1.0)
        doc = run(["sudo", "python", str(DOCTOR), "--port", args.port], HUSKY, timeout=45)
        print(doc.stdout, doc.stderr)
        if doc.returncode not in (0, 2):
            failures.append("doctor")

    evidence = {
        "board": "sen0305",
        "port": args.port,
        "image_sha256": sha,
        "image_bytes": IMAGE.stat().st_size,
        "recorded_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "failures": failures,
        "boot_log_excerpt": log[-2000:] if log else "",
    }
    out = HUSKY / "docs" / "evidence" / "dristy-sen0305-smoke.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(evidence, indent=2), encoding="utf-8")
    print(f"[OK] evidence {out}")

    if failures:
        print("[FAIL]", failures, file=sys.stderr)
        return 1
    print("[PASS] dristy_hw_verify")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
