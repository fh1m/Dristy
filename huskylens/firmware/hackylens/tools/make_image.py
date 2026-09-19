#!/usr/bin/env python3
"""Create a small metadata sidecar for HackyLens firmware images."""

from __future__ import annotations

import argparse
import re
import shutil
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from board_contract import ContractError, load_board
from firmware_attestation import (
    AttestationError,
    read_and_validate as read_and_validate_attestation,
)
from firmware_sidecar import SidecarError, write as write_sidecar
from gen_flash_layout import load_validated, partition_by_name

K210_IMAGE_OVERHEAD = 37
SAFE_IMAGE_NAME_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*\.bin$")
WINDOWS_RESERVED_NAMES = {
    "con", "prn", "aux", "nul",
    *(f"com{number}" for number in range(1, 10)),
    *(f"lpt{number}" for number in range(1, 10)),
}


def validate_output_name(name: str) -> str:
    """Accept one portable basename and never a path or sidecar filename."""

    if (
        not name
        or "/" in name
        or "\\" in name
        or ":" in name
        or Path(name).name != name
        or SAFE_IMAGE_NAME_RE.fullmatch(name) is None
        or name.split(".", 1)[0].casefold() in WINDOWS_RESERVED_NAMES
    ):
        raise ValueError(
            "--name must be a safe portable basename ending in .bin"
        )
    return name


def validate_output_paths(directory: Path, paths: tuple[Path, ...]) -> None:
    """Keep resolved outputs under the selected directory and mutually distinct."""

    root = directory.resolve()
    resolved: list[Path] = []
    for path in paths:
        candidate = path.resolve()
        try:
            candidate.relative_to(root)
        except ValueError as exc:
            raise ValueError(
                f"output path escapes --out-dir after resolution: {path}"
            ) from exc
        resolved.append(candidate)
    if len(set(resolved)) != len(resolved):
        raise ValueError("image, sidecar, and attestation output paths must differ")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Prepare a HackyLens firmware image artifact")
    parser.add_argument("input", type=Path)
    parser.add_argument("--board", required=True, help="Canonical board.toml ID")
    parser.add_argument(
        "--attestation",
        type=Path,
        help=(
            "Canonical build attestation; defaults to input stem + "
            ".attestation.json"
        ),
    )
    parser.add_argument("--out-dir", type=Path, default=Path("dist"))
    parser.add_argument("--name", default=None)
    args = parser.parse_args(argv)

    try:
        board = load_board(args.board)
    except ContractError as exc:
        print(f"board contract error: {exc}", file=sys.stderr)
        return 2
    if board.support != "runtime":
        print(f"board {board.id!r} is conformance-only; firmware artifacts are forbidden",
              file=sys.stderr)
        return 2

    if not args.input.is_file():
        print(f"input image not found: {args.input}", file=sys.stderr)
        return 1

    attestation = args.attestation or args.input.with_suffix(".attestation.json")
    try:
        read_and_validate_attestation(
            attestation,
            args.input,
            board,
            expected_target="full",
            require_release_qualified=True,
        )
    except AttestationError as exc:
        print(f"build attestation validation failed: {exc}", file=sys.stderr)
        return 2

    flash, partitions = load_validated(board.flash_layout_path)
    firmware = partition_by_name(partitions, "firmware")
    wrapped = args.input.stat().st_size + K210_IMAGE_OVERHEAD
    occupied = ((wrapped + flash["erase_size"] - 1) // flash["erase_size"]
                * flash["erase_size"])
    if firmware["offset"] + occupied > firmware["offset"] + firmware["size"]:
        print("firmware image exceeds the selected board partition", file=sys.stderr)
        return 2
    args.out_dir.mkdir(parents=True, exist_ok=True)
    try:
        out_name = validate_output_name(
            args.name or f"hackylens-full-{board.id}.bin"
        )
    except ValueError as exc:
        print(str(exc), file=sys.stderr)
        return 2
    out_image = args.out_dir / out_name
    meta_path = out_image.with_suffix(".json")
    out_attestation = out_image.with_suffix(".attestation.json")
    try:
        validate_output_paths(
            args.out_dir, (out_image, meta_path, out_attestation)
        )
    except ValueError as exc:
        print(str(exc), file=sys.stderr)
        return 2
    if args.input.resolve() != out_image.resolve():
        shutil.copy2(args.input, out_image)
    if attestation.resolve() != out_attestation.resolve():
        shutil.copy2(attestation, out_attestation)
    try:
        # Revalidate after copying so the published attestation is bound to the
        # exact published bytes, irrespective of input/output filenames.
        read_and_validate_attestation(
            out_attestation,
            out_image,
            board,
            expected_target="full",
            require_release_qualified=True,
        )
        write_sidecar(meta_path, out_image, board)
    except (AttestationError, SidecarError) as exc:
        print(f"artifact metadata validation failed: {exc}", file=sys.stderr)
        return 2

    print(out_image)
    print(meta_path)
    print(out_attestation)
    return 0


if __name__ == "__main__":
    sys.exit(main())
