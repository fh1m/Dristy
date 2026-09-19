#!/usr/bin/env python3
"""Stage and build the HuskyLens K210 ISP stub."""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parent
MAX_STUB_SIZE = 64 * 1024
SDK_COMMIT = "02576ba67e8797444f3ee3f34c625b5ed048e707"
KTOOL_COMMIT = "0345aa90d9b3830641373fb4e3ce4edf45d0a46f"


def find_toolchain() -> Path | None:
    candidates = []
    if os.environ.get("KENDRYTE_TOOLCHAIN_BIN"):
        candidates.append(Path(os.environ["KENDRYTE_TOOLCHAIN_BIN"]))
    candidates.extend([
        ROOT / "_deps" / "kendryte-toolchain" / "bin",
        WORKSPACE / "hackylens" / "_deps" / "kendryte-toolchain" / "bin",
        WORKSPACE / "hackylens-legacy" / "_deps" / "kendryte-toolchain" / "bin",
    ])
    return next((p.resolve() for p in candidates
                 if (p / ("riscv64-unknown-elf-gcc.exe" if os.name == "nt"
                          else "riscv64-unknown-elf-gcc")).is_file()), None)


def run(command: list[str]) -> None:
    print("+", " ".join(command))
    subprocess.run(command, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--headless", action="store_true",
                        help="build a diagnostic stub with LCD calls disabled")
    parser.add_argument("--lcd-init-only", action="store_true",
                        help="initialize ST7789 but skip the first screen draw")
    args = parser.parse_args()
    sdk = ROOT / "third_party" / "minsdk"
    toolchain = find_toolchain()
    if not (sdk / "CMakeLists.txt").is_file() or not toolchain:
        print("Dependencies missing; run tools/bootstrap_deps.py", file=sys.stderr)
        return 1

    stage = sdk / "src" / "isp_stub"
    if stage.exists():
        shutil.rmtree(stage)
    shutil.copytree(ROOT / "src", stage)

    build_dir = ROOT / "build" / "sdk"
    if build_dir.exists():
        shutil.rmtree(build_dir)
    build_dir.mkdir(parents=True)
    cmake = shutil.which("cmake") or r"C:\Program Files\CMake\bin\cmake.exe"
    generator = "MinGW Makefiles" if os.name == "nt" else "Ninja"
    slash = lambda p: str(p).replace("\\", "/")
    run([cmake, "-S", slash(sdk), "-B", slash(build_dir), "-G", generator,
         "-DPROJ=isp_stub", f"-DTOOLCHAIN={slash(toolchain)}",
         f"-DISP_HEADLESS={'ON' if args.headless else 'OFF'}",
         f"-DISP_LCD_INIT_ONLY={'ON' if args.lcd_init_only else 'OFF'}",
         "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"])
    run([cmake, "--build", slash(build_dir), "--parallel"])

    built_bin = build_dir / "isp_stub.bin"
    built_elf = build_dir / "isp_stub"
    built_map = build_dir / "isp_stub.map"
    for path in (built_bin, built_elf, built_map):
        if not path.is_file():
            raise RuntimeError(f"missing build output: {path}")
    if built_bin.stat().st_size > MAX_STUB_SIZE:
        raise RuntimeError(f"stub is {built_bin.stat().st_size} bytes; limit is {MAX_STUB_SIZE}")

    output = ROOT / "build"
    suffix = "_headless" if args.headless else "_lcd_init" if args.lcd_init_only else ""
    output_bin = output / f"isp_prog_huskylens{suffix}.bin"
    shutil.copy2(built_bin, output_bin)
    output_bin.chmod(0o644)
    shutil.copy2(built_elf, output / f"isp_stub{suffix}.elf")
    shutil.copy2(built_map, output / f"isp_stub{suffix}.map")
    if not args.headless and not args.lcd_init_only:
        release_bin = ROOT / "isp_prog_huskylens.bin"
        shutil.copy2(built_bin, release_bin)
        release_bin.chmod(0o644)
    digest = hashlib.sha256(built_bin.read_bytes()).hexdigest()
    print(f"[OK] {built_bin.stat().st_size} bytes")
    print(f"[OK] sha256 {digest}")
    print(f"[INFO] expected SDK commit {SDK_COMMIT}")
    print(f"[INFO] minimal BSP derived from ktool {KTOOL_COMMIT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
