#!/usr/bin/env python3
"""Check the local HackyLens development environment."""

from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parent
LOCAL_DEPS = ROOT / "_deps"
LEGACY_DEPS = WORKSPACE / "hackylens-legacy" / "_deps"


def run_version(cmd: list[str]) -> str:
    try:
        out = subprocess.check_output(cmd, stderr=subprocess.STDOUT, text=True, timeout=5)
        return out.splitlines()[0] if out else ""
    except Exception as exc:
        return f"version check failed: {exc}"


def find_in_deps(names: tuple[str, ...]) -> Path | None:
    for root in (LOCAL_DEPS, LEGACY_DEPS):
        if not root.is_dir():
            continue
        for name in names:
            for path in root.rglob(name):
                if path.is_file():
                    return path.resolve()
    return None


def find_exe(names: tuple[str, ...]) -> Path | None:
    dep = find_in_deps(names)
    if dep:
        return dep
    for name in names:
        found = shutil.which(name)
        if found:
            return Path(found).resolve()
    return None


def find_host_gcc() -> Path | None:
    configured = os.environ.get("CC")
    candidates = ([configured] if configured else []) + ["gcc.exe", "gcc"]
    for candidate in candidates:
        if not candidate:
            continue
        path = Path(candidate)
        if not path.is_file():
            resolved = shutil.which(candidate)
            if not resolved:
                continue
            path = Path(resolved)
        if path.name.lower() in {"gcc", "gcc.exe"}:
            return path.resolve()
    return None


def find_sdk() -> Path | None:
    candidates: list[Path] = []
    if os.environ.get("KENDRYTE_SDK_DIR"):
        candidates.append(Path(os.environ["KENDRYTE_SDK_DIR"]))
    candidates.extend([
        LOCAL_DEPS / "kendryte-standalone-sdk",
        LEGACY_DEPS / "kendryte-standalone-sdk",
    ])
    for path in candidates:
        if (path / "CMakeLists.txt").is_file() and (path / "lib").is_dir():
            return path.resolve()
    return None


def find_dependency(env_name: str, directory: str, marker: Path) -> Path | None:
    candidates: list[Path] = []
    if os.environ.get(env_name):
        candidates.append(Path(os.environ[env_name]))
    candidates.extend([LOCAL_DEPS / directory, LEGACY_DEPS / directory])
    for path in candidates:
        if (path / marker).is_file():
            return path.resolve()
    return None


def status(ok: bool, name: str, detail: str = "") -> bool:
    print(f"[{'OK' if ok else 'MISS'}] {name}: {detail}")
    return ok


def main() -> int:
    version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    print(f"HackyLens {version} full environment check")
    print("=======================================")
    ok = True

    cmake = find_exe(("cmake.exe", "cmake"))
    ok &= status(cmake is not None, "cmake", str(cmake) if cmake else "not found")
    if cmake:
        print("       " + run_version([str(cmake), "--version"]))

    ninja = find_exe(("ninja.exe", "ninja"))
    ok &= status(ninja is not None, "ninja", str(ninja) if ninja else "not found")
    if ninja:
        print("       " + run_version([str(ninja), "--version"]))

    make = find_exe(("mingw32-make.exe", "mingw32-make", "make.exe", "make"))
    ok &= status(make is not None, "GNU make", str(make) if make else "not found")
    if make:
        print("       " + run_version([str(make), "--version"]))

    host_gcc = find_host_gcc()
    ok &= status(host_gcc is not None, "host GCC (C harness tests)",
                 str(host_gcc) if host_gcc else "not found on PATH/CC")
    if host_gcc:
        print("       " + run_version([str(host_gcc), "--version"]))

    gcc = find_exe(("riscv64-unknown-elf-gcc.exe", "riscv64-unknown-elf-gcc"))
    ok &= status(gcc is not None, "riscv64-unknown-elf-gcc", str(gcc) if gcc else "not found")
    if gcc:
        print("       " + run_version([str(gcc), "--version"]))

    objcopy = find_exe(("riscv64-unknown-elf-objcopy.exe", "riscv64-unknown-elf-objcopy"))
    ok &= status(objcopy is not None, "riscv64-unknown-elf-objcopy", str(objcopy) if objcopy else "not found")
    if objcopy:
        print("       " + run_version([str(objcopy), "--version"]))

    sdk = find_sdk()
    ok &= status(sdk is not None, "KENDRYTE_SDK_DIR", str(sdk) if sdk else "not found")

    micropython = find_dependency(
        "HACKYLENS_MICROPYTHON_DIR", "micropython", Path("ports/embed/embed.mk"))
    ok &= status(micropython is not None, "MicroPython embed",
                 str(micropython) if micropython else "not found")

    littlefs = find_dependency(
        "HACKYLENS_LITTLEFS_DIR", "littlefs", Path("lfs.c"))
    ok &= status(littlefs is not None, "littlefs",
                 str(littlefs) if littlefs else "not found")

    pyserial_ok = importlib.util.find_spec("serial") is not None
    ok &= status(pyserial_ok, "pyserial", "import serial" if pyserial_ok else "not installed")

    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
