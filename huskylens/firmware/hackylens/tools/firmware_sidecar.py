#!/usr/bin/env python3
"""Strict schema-1 metadata shared by image, package, and flash tools."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
from typing import Any

from board_contract import Board
from gen_flash_layout import load_validated, partition_by_name


ROOT = Path(__file__).resolve().parents[1]
SCHEMA_VERSION = 1
BOARD_CONTRACT_VERSION = "0.1.0"
BUILD_PROFILE = "hackylens-full"
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
ADDRESS_RE = re.compile(r"^0x[0-9A-F]{8}$")
REQUIRED_FIELDS = {
    "schema",
    "firmware_version",
    "board_id",
    "platform_id",
    "board_contract_version",
    "build_profile",
    "image",
    "size",
    "sha256",
    "flash_address",
    "flash_layout_sha256",
}
OPTIONAL_FIELDS = {"runtime_profile"}


class SidecarError(ValueError):
    """A firmware image sidecar is absent, malformed, or mismatched."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def current_firmware_version(root: Path = ROOT) -> str:
    try:
        version = (root / "VERSION").read_text(encoding="utf-8").strip()
    except OSError as exc:
        raise SidecarError(f"cannot read canonical VERSION: {exc}") from exc
    if not version:
        raise SidecarError("canonical VERSION is empty")
    return version


def layout_identity(board: Board) -> tuple[str, str]:
    # Validation happens before hashing so non-canonical layout bytes can never
    # acquire a trusted identity.
    _, partitions = load_validated(board.flash_layout_path)
    firmware = partition_by_name(partitions, "firmware")
    address = f"0x{firmware['offset']:08X}"
    return address, sha256_file(board.flash_layout_path)


def build_document(image: Path, board: Board) -> dict[str, object]:
    if not image.is_file():
        raise SidecarError(f"firmware image not found: {image}")
    address, layout_hash = layout_identity(board)
    document: dict[str, object] = {
        "schema": SCHEMA_VERSION,
        "firmware_version": current_firmware_version(),
        "board_id": board.id,
        "platform_id": board.registry.platform,
        "board_contract_version": BOARD_CONTRACT_VERSION,
        "build_profile": BUILD_PROFILE,
        "image": image.name,
        "size": image.stat().st_size,
        "sha256": sha256_file(image),
        "flash_address": address,
        "flash_layout_sha256": layout_hash,
    }
    if board.runtime_profile is not None:
        document["runtime_profile"] = board.runtime_profile
    return document


def _nonempty_string(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value:
        raise SidecarError(f"{field}: expected non-empty string")
    return value


def validate_document(document: Any, image: Path, board: Board,
                      *, expected_flash_address: int | None = None) -> None:
    if not isinstance(document, dict):
        raise SidecarError("root: expected object")
    unknown = sorted(set(document) - REQUIRED_FIELDS - OPTIONAL_FIELDS)
    missing = sorted(REQUIRED_FIELDS - set(document))
    if unknown:
        raise SidecarError("root: unknown field(s): " + ", ".join(unknown))
    if missing:
        raise SidecarError("root: missing field(s): " + ", ".join(missing))
    if type(document["schema"]) is not int or document["schema"] != SCHEMA_VERSION:
        raise SidecarError(f"schema: expected integer {SCHEMA_VERSION}")
    for field in REQUIRED_FIELDS - {"schema", "size"}:
        _nonempty_string(document[field], field)
    size = document["size"]
    if isinstance(size, bool) or not isinstance(size, int) or size <= 0:
        raise SidecarError("size: expected positive integer")
    for field in ("sha256", "flash_layout_sha256"):
        if SHA256_RE.fullmatch(document[field]) is None:
            raise SidecarError(f"{field}: expected lowercase SHA-256")
    if ADDRESS_RE.fullmatch(document["flash_address"]) is None:
        raise SidecarError("flash_address: expected 0x plus eight uppercase hex digits")
    if "runtime_profile" in document:
        _nonempty_string(document["runtime_profile"], "runtime_profile")

    expected = build_document(image, board)
    if expected_flash_address is not None:
        expected["flash_address"] = f"0x{expected_flash_address:08X}"
    expected_fields = set(REQUIRED_FIELDS)
    if board.runtime_profile is not None:
        expected_fields.add("runtime_profile")
    elif "runtime_profile" in document:
        raise SidecarError("runtime_profile: forbidden for this board")
    mismatches = [
        f"{field}: expected {expected[field]!r}, got {document.get(field)!r}"
        for field in sorted(expected_fields)
        if document.get(field) != expected[field]
    ]
    if mismatches:
        raise SidecarError("sidecar mismatch: " + "; ".join(mismatches))


def read_and_validate(path: Path, image: Path, board: Board,
                      *, expected_flash_address: int | None = None) -> dict[str, Any]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise SidecarError(f"cannot read firmware sidecar {path}: {exc}") from exc
    validate_document(
        document,
        image,
        board,
        expected_flash_address=expected_flash_address,
    )
    return document


def write(path: Path, image: Path, board: Board) -> dict[str, object]:
    if path.resolve() == image.resolve():
        raise SidecarError("firmware sidecar path must differ from firmware image")
    document = build_document(image, board)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(document, ensure_ascii=False, allow_nan=False,
                   indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    return document
