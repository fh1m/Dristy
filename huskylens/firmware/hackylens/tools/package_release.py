#!/usr/bin/env python3
"""Create versioned HackyLens firmware and SD-card artifacts."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys
import zipfile


ROOT = Path(__file__).resolve().parents[1]
TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from board_contract import Board, ContractError, load_board
from firmware_attestation import (
    AttestationError,
    read_and_validate as read_and_validate_attestation,
)
from firmware_sidecar import (
    SidecarError,
    read_and_validate as read_and_validate_sidecar,
    write as write_sidecar,
)
from gen_flash_layout import load_layout, load_validated, partition_by_name
from bootstrap_deps import LITTLEFS_REVISION, MICROPYTHON_REVISION

K210_IMAGE_OVERHEAD = 37


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_release_firmware(path: Path, board: Board) -> None:
    """Validate partition safety; build identity comes only from attestation."""

    wrapped_size = path.stat().st_size + K210_IMAGE_OVERHEAD
    flash, partitions = load_validated(board.flash_layout_path)
    firmware_partition = partition_by_name(partitions, "firmware")
    erased_size = (
        (wrapped_size + flash["erase_size"] - 1) // flash["erase_size"]
        * flash["erase_size"]
    )
    if erased_size > firmware_partition["size"]:
        raise SystemExit(
            "firmware image exceeds canonical firmware partition: "
            f"raw={path.stat().st_size} wrapped={wrapped_size} erase={erased_size} "
            f"limit={firmware_partition['size']}"
        )


def ensure_allowlisted_output(directory: Path, expected_names: set[str]) -> None:
    """Refuse to publish a directory containing an unrecognised artifact."""

    if not directory.exists():
        return
    unexpected = sorted(
        path.relative_to(directory).as_posix()
        for path in directory.rglob("*")
        if path.is_file() and path.relative_to(directory).as_posix() not in expected_names
    )
    if unexpected:
        raise SystemExit(
            "release output directory is not clean; refusing unexpected files: "
            + ", ".join(unexpected)
        )


def validate_output_paths(directory: Path, paths: list[Path]) -> None:
    """Reject symlinked or escaping release outputs before any write."""

    root = directory.resolve()
    resolved: list[Path] = []
    for path in paths:
        if path.is_symlink():
            raise SystemExit(f"release output path must not be a symlink: {path}")
        candidate = path.resolve()
        try:
            candidate.relative_to(root)
        except ValueError as exc:
            raise SystemExit(
                f"release output path escapes --out-dir after resolution: {path}"
            ) from exc
        resolved.append(candidate)
    if len(set(resolved)) != len(resolved):
        raise SystemExit("release output paths must be mutually distinct")


def firmware_dependency_manifest(
    version: str, embed: Path | None = None
) -> dict[str, object]:
    embed = embed or ROOT / "build" / "micropython_embed"
    if not embed.is_dir():
        raise SystemExit(
            "generated MicroPython embed package is missing; build full firmware first"
        )
    micropython_sources = sorted(
        path.relative_to(embed).as_posix()
        for path in embed.rglob("*.c")
        if path.relative_to(embed).as_posix() != "port/mphalport.c"
    )
    if not micropython_sources:
        raise SystemExit("generated MicroPython embed source list is empty")
    return {
        "schema_version": 1,
        "project": "HackyLens firmware",
        "firmware_version": version,
        "dependencies": [
            {
                "name": "MicroPython",
                "version": "1.28.0",
                "revision": MICROPYTHON_REVISION,
                "license": "MIT",
                "license_file": "licenses/MicroPython-LICENSE",
                "included_source_files": micropython_sources,
                "excluded_generated_port": "port/mphalport.c",
                "local_patch": (
                    "firmware/third_party/micropython/patches/"
                    "0001-poll-native-iterators.patch"
                ),
            },
            {
                "name": "littlefs",
                "version": "2.11.2",
                "revision": LITTLEFS_REVISION,
                "license": "BSD-3-Clause",
                "license_file": "licenses/littlefs-LICENSE.md",
                "included_source_files": [
                    "lfs.c", "lfs.h", "lfs_util.c", "lfs_util.h"
                ],
            },
        ],
    }


def write_firmware_notices(
    manifest_path: Path, archive_path: Path, version: str
) -> None:
    manifest = firmware_dependency_manifest(version)
    manifest_text = json.dumps(manifest, indent=2) + "\n"
    manifest_path.write_text(manifest_text, encoding="utf-8")

    dependency_notice = ROOT / "firmware" / "third_party" / "DEPENDENCIES.md"
    micropython_license = (
        ROOT / "firmware" / "third_party" / "micropython" / "LICENSE"
    )
    littlefs_license = (
        ROOT / "firmware" / "third_party" / "littlefs" / "LICENSE.md"
    )
    required = (dependency_notice, micropython_license, littlefs_license)
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise SystemExit("firmware third-party notices are incomplete: " + ", ".join(missing))
    with zipfile.ZipFile(
        archive_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9
    ) as archive:
        archive.writestr("THIRD_PARTY_MANIFEST.json", manifest_text)
        archive.write(dependency_notice, "DEPENDENCIES.md")
        archive.write(micropython_license, "licenses/MicroPython-LICENSE")
        archive.write(littlefs_license, "licenses/littlefs-LICENSE.md")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Package a HackyLens release")
    parser.add_argument("--board", required=True, help="Canonical board.toml ID")
    parser.add_argument("--firmware", type=Path)
    parser.add_argument(
        "--sidecar",
        type=Path,
        help="Verified schema-1 firmware sidecar; defaults to firmware stem + .json",
    )
    parser.add_argument(
        "--attestation",
        type=Path,
        help=(
            "Canonical release-qualified build attestation; defaults to "
            "firmware stem + .attestation.json"
        ),
    )
    parser.add_argument("--sdcard", type=Path, default=ROOT / "sdcard")
    parser.add_argument("--out-dir", type=Path,
                        default=ROOT / "dist" / "release")
    parser.add_argument("--tag", help="Expected release tag, for example v0.4.0")
    args = parser.parse_args(argv)

    try:
        board = load_board(args.board)
    except ContractError as exc:
        raise SystemExit(f"board contract error: {exc}") from exc
    if not board.releaseable:
        raise SystemExit(f"board {board.id!r} is not releaseable")
    if args.firmware is None:
        args.firmware = ROOT / "dist" / f"hackylens-full-{board.id}.bin"

    version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    if not version or (args.tag and args.tag != f"v{version}"):
        raise SystemExit(
            f"release tag {args.tag!r} does not match VERSION={version!r}")
    if not args.firmware.is_file():
        raise SystemExit(f"firmware image not found: {args.firmware}")
    validate_release_firmware(args.firmware, board)
    source_sidecar = args.sidecar or args.firmware.with_suffix(".json")
    if not source_sidecar.is_file():
        raise SystemExit(f"verified firmware sidecar not found: {source_sidecar}")
    try:
        read_and_validate_sidecar(source_sidecar, args.firmware, board)
    except SidecarError as exc:
        raise SystemExit(f"firmware sidecar validation failed: {exc}") from exc
    source_attestation = (
        args.attestation or args.firmware.with_suffix(".attestation.json")
    )
    if not source_attestation.is_file():
        raise SystemExit(
            f"release-qualified build attestation not found: {source_attestation}"
        )
    try:
        read_and_validate_attestation(
            source_attestation,
            args.firmware,
            board,
            expected_target="full",
            require_release_qualified=True,
        )
    except AttestationError as exc:
        raise SystemExit(f"build attestation validation failed: {exc}") from exc
    if not args.sdcard.is_dir():
        raise SystemExit(f"SD-card tree not found: {args.sdcard}")
    flash_layout = load_layout(board.flash_layout_path)
    flash, partitions = load_validated(board.flash_layout_path)
    firmware_partition = partition_by_name(partitions, "firmware")

    artifact_stem = f"hackylens-{board.id}-v{version}"
    firmware_out = args.out_dir / f"{artifact_stem}.bin"
    sdcard_out = args.out_dir / f"{artifact_stem}-sdcard.zip"
    metadata_out = args.out_dir / f"{artifact_stem}.json"
    attestation_out = args.out_dir / f"{artifact_stem}-attestation.json"
    release_manifest_out = args.out_dir / f"{artifact_stem}-release.json"
    dependencies_out = (
        args.out_dir / f"{artifact_stem}-third-party.json"
    )
    notices_out = (
        args.out_dir / f"{artifact_stem}-third-party-notices.zip"
    )
    sums_out = args.out_dir / "SHA256SUMS.txt"
    expected_names = {
        firmware_out.name,
        sdcard_out.name,
        metadata_out.name,
        attestation_out.name,
        release_manifest_out.name,
        dependencies_out.name,
        notices_out.name,
        sums_out.name,
    }
    ensure_allowlisted_output(args.out_dir, expected_names)
    args.out_dir.mkdir(parents=True, exist_ok=True)
    validate_output_paths(
        args.out_dir,
        [
            firmware_out, sdcard_out, metadata_out, attestation_out,
            release_manifest_out, dependencies_out, notices_out, sums_out,
        ],
    )

    shutil.copy2(args.firmware, firmware_out)
    shutil.copy2(source_attestation, attestation_out)
    try:
        read_and_validate_attestation(
            attestation_out,
            firmware_out,
            board,
            expected_target="full",
            require_release_qualified=True,
        )
    except AttestationError as exc:
        raise SystemExit(f"copied build attestation validation failed: {exc}") from exc
    write_sidecar(metadata_out, firmware_out, board)
    with zipfile.ZipFile(sdcard_out, "w", compression=zipfile.ZIP_DEFLATED,
                         compresslevel=9) as archive:
        for path in sorted(args.sdcard.rglob("*")):
            if path.is_file():
                archive.write(path, Path("sdcard") / path.relative_to(args.sdcard))
    write_firmware_notices(dependencies_out, notices_out, version)

    release_manifest = {
        "project": "HackyLens",
        "schema": 1,
        "version": version,
        "board_id": board.id,
        "platform_id": board.registry.platform,
        "firmware": firmware_out.name,
        "firmware_bytes": firmware_out.stat().st_size,
        "firmware_sha256": sha256(firmware_out),
        "firmware_sidecar": metadata_out.name,
        "firmware_sidecar_sha256": sha256(metadata_out),
        "firmware_attestation": attestation_out.name,
        "firmware_attestation_sha256": sha256(attestation_out),
        "sdcard": sdcard_out.name,
        "sdcard_sha256": sha256(sdcard_out),
        "flash_address": f"0x{firmware_partition['offset']:08X}",
        "flash_layout_sha256": hashlib.sha256(
            board.flash_layout_path.read_bytes()
        ).hexdigest(),
        "flash_layout": flash_layout,
        "firmware_dependencies": dependencies_out.name,
        "firmware_dependencies_sha256": sha256(dependencies_out),
        "firmware_notices": notices_out.name,
        "firmware_notices_sha256": sha256(notices_out),
        "flasher_policy": {
            "firmware_max_exclusive": (
                f"0x{firmware_partition['offset'] + firmware_partition['size']:08X}"
            ),
            "preserve_partitions": [
                partition["name"] for partition in partitions
                if partition["name"] != "firmware"
            ],
            "erase_size": f"0x{flash['erase_size']:08X}",
            "k210_image_overhead_bytes": K210_IMAGE_OVERHEAD,
        },
    }
    release_manifest_out.write_text(
        json.dumps(release_manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    artifacts = [
        firmware_out, attestation_out, sdcard_out, dependencies_out, notices_out
    ]
    artifacts.extend((metadata_out, release_manifest_out))
    sums_out.write_text(
        "".join(f"{sha256(path)}  {path.name}\n" for path in artifacts),
        encoding="ascii")
    for path in (*artifacts, sums_out):
        print(path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
