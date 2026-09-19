#!/usr/bin/env python3
"""Bootstrap local Dristy dependencies under dristy/_deps."""

from __future__ import annotations

import argparse
import ast
import binascii
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEPS = ROOT / "_deps"
DOWNLOADS = DEPS / "downloads"

SDK_DIR = DEPS / "kendryte-standalone-sdk"
KFLASH_REF_DIR = DEPS / "kflash.py-reference"
MICROPYTHON_DIR = DEPS / "micropython"
LITTLEFS_DIR = DEPS / "littlefs"
ISP_STUB = DEPS / "isp_prog.bin"

SDK_REPO = "https://github.com/kendryte/kendryte-standalone-sdk"
KFLASH_REPO = "https://github.com/sipeed/kflash.py"
MICROPYTHON_REPO = "https://github.com/micropython/micropython"
LITTLEFS_REPO = "https://github.com/littlefs-project/littlefs"
SDK_REVISION = "02576ba67e8797444f3ee3f34c625b5ed048e707"
KFLASH_REVISION = "550828c768b16ef329695d3f5eace3f6bcf14af2"
MICROPYTHON_REVISION = "e0e9fbb17ed6fd06bb76e266ae554784c9c80804"  # v1.28.0
LITTLEFS_REVISION = "adad0fbbcf5382c20978d07f94f9c13be9041c1b"  # v2.11.2
WINDOWS = os.name == "nt"

# The Kendryte release publishes a build per host platform. Only the archive
# name, hash and extracted layout differ; the RISC-V compiler inside is the
# same 8.2.0.
TOOLCHAIN_RELEASE = (
    "https://github.com/kendryte/kendryte-gnu-toolchain/releases/download/"
    "v8.2.0-20190409/"
)
if WINDOWS:
    TOOLCHAIN_NAME = "kendryte-toolchain-win-i386-8.2.0-20190409.tar.xz"
    TOOLCHAIN_SHA256 = (
        "ec8aa2bd3f1a42f4d227696c0d6d9baf32bef0e725d24d23b8e955521d4ae89e"
    )
else:
    TOOLCHAIN_NAME = "kendryte-toolchain-ubuntu-amd64-8.2.0-20190409.tar.xz"
    TOOLCHAIN_SHA256 = (
        "6b7b9f654df339cfb5da85f7db6f598552196c499eb244f548eaf73c63b81072"
    )
TOOLCHAIN_URL = TOOLCHAIN_RELEASE + TOOLCHAIN_NAME
TOOLCHAIN_ARCHIVE = DOWNLOADS / TOOLCHAIN_NAME

# The host compiler builds MicroPython's code generators, which run on the
# build machine rather than the K210. On Windows a specific mingw-w64 build is
# pinned and fetched. On POSIX the system compiler is already a native ELF
# toolchain, so it is used directly and only checked for existence -- pinning a
# distribution's GCC version would reject every supported distribution.
HOST_GCC_VERSION = "13.2.0"
HOST_GCC_MACHINE = "x86_64-w64-mingw32"
HOST_GCC_URL = (
    "https://github.com/niXman/mingw-builds-binaries/releases/download/"
    "13.2.0-rt_v11-rev0/"
    "x86_64-13.2.0-release-posix-seh-ucrt-rt_v11-rev0.7z"
)
HOST_GCC_ARCHIVE = DOWNLOADS / (
    "x86_64-13.2.0-release-posix-seh-ucrt-rt_v11-rev0.7z"
)
HOST_GCC_SHA256 = "a05578fab9068c678ce761e7b2e284d23fa7416a2f4f36349e05e506c944e2c7"
HOST_GCC_DIR = DEPS / f"host-gcc-{HOST_GCC_VERSION}"
HOST_GCC_EXE = HOST_GCC_DIR / "mingw64" / "bin" / "gcc.exe"


def run(cmd: list[str], cwd: Path | None = None) -> None:
    print("+ " + " ".join(cmd))
    subprocess.run(cmd, cwd=cwd, check=True)


def require_git() -> str:
    git = shutil.which("git")
    if not git:
        raise RuntimeError("git is required to validate pinned dependencies")
    return git


def git_output(git: str, path: Path, *args: str) -> str:
    result = subprocess.run(
        [git, "-C", str(path), *args],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    return result.stdout.strip()


def validate_git_checkout(path: Path, revision: str) -> None:
    git = require_git()
    if not path.is_dir():
        raise RuntimeError(f"missing Git checkout: {path}")
    try:
        inside = git_output(git, path, "rev-parse", "--is-inside-work-tree")
        head = git_output(git, path, "rev-parse", "HEAD").lower()
        dirty = git_output(
            git, path, "status", "--porcelain=v1", "--untracked-files=all"
        )
    except subprocess.CalledProcessError as exc:
        raise RuntimeError(f"invalid Git checkout: {path}: {exc.stderr.strip()}") from exc
    if inside != "true":
        raise RuntimeError(f"dependency is not a Git worktree: {path}")
    if head != revision.lower():
        raise RuntimeError(
            f"dependency revision mismatch at {path}: expected {revision}, got {head}"
        )
    if dirty:
        raise RuntimeError(f"dependency checkout is dirty: {path}:\n{dirty}")
    print(f"[OK] pinned clean checkout: {path} @ {head}")


def ensure_git_checkout(path: Path, repo: str, revision: str) -> None:
    git = require_git()
    if path.exists():
        if not path.is_dir() or not (path / ".git").exists():
            raise RuntimeError(
                f"refusing non-Git dependency directory; remove it and retry: {path}"
            )
        print(f"[OK] checkout exists: {path}")
    else:
        path.parent.mkdir(parents=True, exist_ok=True)
        run([git, "clone", "--no-checkout", repo, str(path)])

    try:
        git_output(git, path, "cat-file", "-e", f"{revision}^{{commit}}")
    except subprocess.CalledProcessError:
        run([git, "-C", str(path), "fetch", "--no-tags", "origin", revision])
    # Dependency trees are disposable caches. Reset both tracked and ignored
    # build outputs so a restored CI cache is byte-for-byte the pinned tree.
    run([git, "-C", str(path), "checkout", "--detach", "--force", revision])
    run([git, "-C", str(path), "reset", "--hard", revision])
    run([git, "-C", str(path), "clean", "-ffdx"])
    validate_git_checkout(path, revision)


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify_sha256(path: Path, expected: str) -> None:
    actual = file_sha256(path)
    if actual.lower() != expected.lower():
        raise RuntimeError(
            f"SHA-256 mismatch for {path}: expected {expected.lower()}, got {actual}"
        )


def download_file(url: str, out: Path, sha256: str, offline: bool = False) -> None:
    if out.is_file() and out.stat().st_size > 0:
        try:
            verify_sha256(out, sha256)
        except RuntimeError:
            if offline:
                raise
            print(f"[STALE] cached download failed SHA-256 verification: {out}")
            out.unlink()
        else:
            print(f"[OK] verified download: {out}")
            return
    if offline:
        raise RuntimeError(f"offline download cache missing: {out}")
    out.parent.mkdir(parents=True, exist_ok=True)
    tmp = out.with_suffix(out.suffix + ".part")
    print(f"[GET] {url}")
    try:
        with urllib.request.urlopen(url) as src, tmp.open("wb") as dst:
            shutil.copyfileobj(src, dst, length=1024 * 1024)
        verify_sha256(tmp, sha256)
        tmp.replace(out)
    except Exception:
        tmp.unlink(missing_ok=True)
        raise


def safe_extract_tar(archive: Path, destination: Path) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    root = destination.resolve()
    with tarfile.open(archive, "r:*") as tar:
        members = tar.getmembers()
        for member in members:
            target = (destination / member.name).resolve()
            if root != target and root not in target.parents:
                raise RuntimeError(f"refusing unsafe tar member: {member.name}")
        tar.extractall(destination, members=members)


def find_7zip() -> Path | None:
    candidates = [shutil.which("7z.exe"), shutil.which("7z")]
    program_files = os.environ.get("ProgramFiles")
    if program_files:
        candidates.append(str(Path(program_files) / "7-Zip" / "7z.exe"))
    candidates.append(r"C:\Program Files\7-Zip\7z.exe")
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return Path(candidate)
    return None


def host_gcc_identity(gcc: Path) -> tuple[str, str]:
    def output(*args: str) -> str:
        result = subprocess.run(
            [str(gcc), *args],
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        return result.stdout.strip()

    return output("-dumpfullversion", "-dumpversion"), output("-dumpmachine")


def validate_host_gcc(gcc: Path) -> None:
    if not gcc.is_file():
        raise RuntimeError(f"pinned host GCC is missing: {gcc}")
    try:
        version, machine = host_gcc_identity(gcc)
    except (OSError, subprocess.CalledProcessError) as exc:
        raise RuntimeError(f"cannot execute pinned host GCC {gcc}: {exc}") from exc
    if version != HOST_GCC_VERSION:
        raise RuntimeError(
            f"host GCC version mismatch: expected {HOST_GCC_VERSION}, got {version}"
        )
    if machine != HOST_GCC_MACHINE:
        raise RuntimeError(
            f"host GCC target mismatch: expected {HOST_GCC_MACHINE}, got {machine}"
        )
    print(f"[OK] pinned host GCC: {gcc} ({version}, {machine})")


def find_system_gcc() -> Path:
    """Locate a native host compiler on POSIX.

    MicroPython's qstr and frozen-module generators are compiled for and run on
    the build machine, so any working native GCC or Clang serves. Unlike the
    Windows path there is nothing to download.
    """
    for name in ("cc", "gcc", "clang"):
        found = shutil.which(name)
        if found:
            gcc = Path(found)
            try:
                version, machine = host_gcc_identity(gcc)
            except (OSError, subprocess.CalledProcessError):
                continue
            print(f"[OK] host compiler: {gcc} ({version}, {machine})")
            return gcc
    raise RuntimeError(
        "no native host compiler found; install build-essential (cc/gcc/clang)"
    )


def ensure_host_gcc(offline: bool = False) -> Path:
    if not WINDOWS:
        return find_system_gcc()
    download_file(HOST_GCC_URL, HOST_GCC_ARCHIVE, HOST_GCC_SHA256, offline)
    if HOST_GCC_EXE.is_file():
        validate_host_gcc(HOST_GCC_EXE)
        return HOST_GCC_EXE

    seven_zip = find_7zip()
    if not seven_zip:
        raise RuntimeError(
            "7-Zip is required to extract the pinned Windows host GCC archive"
        )

    extracting = HOST_GCC_DIR.with_name(HOST_GCC_DIR.name + ".extracting")
    if extracting.exists():
        shutil.rmtree(extracting)
    if HOST_GCC_DIR.exists():
        shutil.rmtree(HOST_GCC_DIR)
    extracting.mkdir(parents=True)
    print(f"[EXTRACT] {HOST_GCC_ARCHIVE}")
    try:
        run([str(seven_zip), "x", "-y", f"-o{extracting}", str(HOST_GCC_ARCHIVE)])
        extracted_gcc = extracting / "mingw64" / "bin" / "gcc.exe"
        validate_host_gcc(extracted_gcc)
        extracting.replace(HOST_GCC_DIR)
    except Exception:
        shutil.rmtree(extracting, ignore_errors=True)
        raise

    validate_host_gcc(HOST_GCC_EXE)
    return HOST_GCC_EXE


def find_toolchain_bin() -> Path | None:
    names = ("riscv64-unknown-elf-gcc.exe", "riscv64-unknown-elf-gcc")
    for name in names:
        for gcc in DEPS.rglob(name):
            if gcc.is_file():
                return gcc.parent
    return None


def ensure_toolchain(offline: bool = False) -> Path:
    # Keep the original, revision-named archive in the cache and authenticate it
    # even when an extracted compiler is already present.
    download_file(TOOLCHAIN_URL, TOOLCHAIN_ARCHIVE, TOOLCHAIN_SHA256, offline)
    existing = find_toolchain_bin()
    if existing:
        print(f"[OK] toolchain: {existing}")
        return existing

    print(f"[EXTRACT] {TOOLCHAIN_ARCHIVE}")
    safe_extract_tar(TOOLCHAIN_ARCHIVE, DEPS)

    found = find_toolchain_bin()
    if not found:
        raise RuntimeError("toolchain extracted, but riscv64-unknown-elf-gcc was not found")
    print(f"[OK] toolchain: {found}")
    return found


def extract_isp_stub() -> None:
    if ISP_STUB.is_file() and ISP_STUB.stat().st_size > 0:
        print(f"[OK] ISP stub: {ISP_STUB}")
        return

    source = KFLASH_REF_DIR / "kflash.py"
    if not source.is_file():
        raise RuntimeError(f"kflash reference missing: {source}")

    text = source.read_text(encoding="utf-8")
    match = re.search(r"ISP_PROG\s*=\s*('(?:[^'\\]|\\.)*')", text)
    if not match:
        raise RuntimeError("could not locate ISP_PROG in kflash.py reference")

    compressed_hex = ast.literal_eval(match.group(1))
    data = zlib.decompress(binascii.unhexlify(compressed_hex))
    ISP_STUB.write_bytes(data)
    print(f"[OK] ISP stub: {ISP_STUB} ({len(data)} bytes)")


def write_env(toolchain_bin: Path, host_gcc: Path) -> None:
    if not WINDOWS:
        write_env_sh(toolchain_bin, host_gcc)
        return
    env_path = ROOT / "env.ps1"
    sdk = SDK_DIR.resolve()
    toolchain = toolchain_bin.resolve()
    host_compiler = host_gcc.resolve()
    host_bin = host_compiler.parent
    content = f"""# Generated by dristy/tools/bootstrap_deps.py
$env:KENDRYTE_SDK_DIR = "{sdk}"
$env:KENDRYTE_TOOLCHAIN_BIN = "{toolchain}"
$env:DRISTY_MICROPYTHON_DIR = "{MICROPYTHON_DIR.resolve()}"
$env:DRISTY_LITTLEFS_DIR = "{LITTLEFS_DIR.resolve()}"
$env:CC = "{host_compiler}"
$env:Path = "{host_bin};{toolchain};" + $env:Path
Write-Host "[Dristy] KENDRYTE_SDK_DIR=$env:KENDRYTE_SDK_DIR"
Write-Host "[Dristy] KENDRYTE_TOOLCHAIN_BIN=$env:KENDRYTE_TOOLCHAIN_BIN"
Write-Host "[Dristy] DRISTY_MICROPYTHON_DIR=$env:DRISTY_MICROPYTHON_DIR"
Write-Host "[Dristy] DRISTY_LITTLEFS_DIR=$env:DRISTY_LITTLEFS_DIR"
Write-Host "[Dristy] CC=$env:CC"
"""
    env_path.write_text(content, encoding="utf-8")
    print(f"[OK] env: {env_path}")


def write_env_sh(toolchain_bin: Path, host_gcc: Path) -> None:
    """Emit the POSIX equivalent of env.ps1.

    The Kendryte toolchain is deliberately not prepended to PATH: its
    riscv64-unknown-elf prefix collides with other RISC-V toolchains that may
    already be installed. build_firmware.py reads KENDRYTE_TOOLCHAIN_BIN.
    """
    env_path = ROOT / "env.sh"
    content = f"""# Generated by dristy/tools/bootstrap_deps.py
export KENDRYTE_SDK_DIR="{SDK_DIR.resolve()}"
export KENDRYTE_TOOLCHAIN_BIN="{toolchain_bin.resolve()}"
export DRISTY_MICROPYTHON_DIR="{MICROPYTHON_DIR.resolve()}"
export DRISTY_LITTLEFS_DIR="{LITTLEFS_DIR.resolve()}"
export CC="{host_gcc.resolve()}"
echo "[Dristy] KENDRYTE_SDK_DIR=$KENDRYTE_SDK_DIR"
echo "[Dristy] KENDRYTE_TOOLCHAIN_BIN=$KENDRYTE_TOOLCHAIN_BIN"
echo "[Dristy] DRISTY_MICROPYTHON_DIR=$DRISTY_MICROPYTHON_DIR"
echo "[Dristy] DRISTY_LITTLEFS_DIR=$DRISTY_LITTLEFS_DIR"
echo "[Dristy] CC=$CC"
"""
    env_path.write_text(content, encoding="utf-8")
    print(f"[OK] env: {env_path}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Bootstrap local Dristy dependencies")
    parser.add_argument("--skip-download", action="store_true", help="Only validate existing dependencies")
    args = parser.parse_args(argv)

    DEPS.mkdir(parents=True, exist_ok=True)
    checkouts = (
        (SDK_DIR, SDK_REPO, SDK_REVISION),
        (KFLASH_REF_DIR, KFLASH_REPO, KFLASH_REVISION),
        (MICROPYTHON_DIR, MICROPYTHON_REPO, MICROPYTHON_REVISION),
        (LITTLEFS_DIR, LITTLEFS_REPO, LITTLEFS_REVISION),
    )
    for path, repo, revision in checkouts:
        if args.skip_download:
            validate_git_checkout(path, revision)
        else:
            ensure_git_checkout(path, repo, revision)

    if not SDK_DIR.is_dir():
        raise SystemExit(f"missing SDK checkout: {SDK_DIR}")
    if not KFLASH_REF_DIR.is_dir():
        raise SystemExit(f"missing kflash reference checkout: {KFLASH_REF_DIR}")
    if not (MICROPYTHON_DIR / "ports" / "embed" / "embed.mk").is_file():
        raise SystemExit(f"missing MicroPython embed checkout: {MICROPYTHON_DIR}")
    if not (LITTLEFS_DIR / "lfs.c").is_file():
        raise SystemExit(f"missing littlefs checkout: {LITTLEFS_DIR}")

    host_gcc = ensure_host_gcc(args.skip_download)
    toolchain = ensure_toolchain(args.skip_download)
    extract_isp_stub()
    write_env(toolchain, host_gcc)

    print("[DONE] run: . .\\env.ps1" if WINDOWS else "[DONE] run: . ./env.sh")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        raise SystemExit(130)
    except Exception as exc:
        print(f"[ERR] {exc}", file=sys.stderr)
        raise SystemExit(1)
