#!/usr/bin/env python3
"""Provision the pinned Kendryte SDK and Windows toolchain."""

from __future__ import annotations

import lzma
import os
from pathlib import Path
import shutil
import tarfile
import urllib.request


ROOT = Path(__file__).resolve().parents[1]
DEPS = ROOT / "_deps"
TOOLCHAIN = DEPS / "kendryte-toolchain"
SDK = DEPS / "kendryte-standalone-sdk"
SDK_COMMIT = "02576ba67e8797444f3ee3f34c625b5ed048e707"
TOOLCHAIN_VERSION = "8.2.0-20190409"
TOOLCHAIN_PLATFORM = "win-amd64" if os.name == "nt" else "ubuntu-amd64"
TOOLCHAIN_URL = (
    "https://github.com/kendryte/kendryte-gnu-toolchain/releases/download/"
    f"v{TOOLCHAIN_VERSION}/kendryte-toolchain-{TOOLCHAIN_PLATFORM}-{TOOLCHAIN_VERSION}.tar.xz"
)
SDK_URL = f"https://github.com/kendryte/kendryte-standalone-sdk/archive/{SDK_COMMIT}.tar.gz"


def main() -> int:
    DEPS.mkdir(parents=True, exist_ok=True)
    gcc_name = "riscv64-unknown-elf-gcc.exe" if os.name == "nt" else "riscv64-unknown-elf-gcc"
    gcc = next(TOOLCHAIN.rglob(gcc_name), None) if TOOLCHAIN.exists() else None
    if gcc is None:
        archive = DEPS / "kendryte-toolchain.tar.xz"
        if not archive.exists():
            urllib.request.urlretrieve(TOOLCHAIN_URL, archive)
        with lzma.open(archive) as compressed, tarfile.open(fileobj=compressed) as tar:
            tar.extractall(DEPS, filter="data")
        extracted = next((path for path in DEPS.glob("kendryte-toolchain-*") if path.is_dir()), None)
        if extracted and extracted != TOOLCHAIN:
            if TOOLCHAIN.exists():
                shutil.rmtree(TOOLCHAIN)
            extracted.rename(TOOLCHAIN)
    if not (SDK / "CMakeLists.txt").is_file():
        archive = DEPS / f"kendryte-sdk-{SDK_COMMIT}.tar.gz"
        if not archive.exists():
            urllib.request.urlretrieve(SDK_URL, archive)
        with tarfile.open(archive, "r:gz") as tar:
            tar.extractall(DEPS, filter="data")
        extracted = DEPS / f"kendryte-standalone-sdk-{SDK_COMMIT}"
        if extracted.is_dir():
            if SDK.exists():
                shutil.rmtree(SDK)
            extracted.rename(SDK)
    print("[OK] dependencies ready")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
