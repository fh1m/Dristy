#!/usr/bin/env python3
"""Canonical build attestations for board-qualified firmware images.

The attestation is private build evidence, not a public firmware contract.  It
is deliberately independent of an artifact pathname: identity comes from the
exact image size/hash and the selected build composition.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
from typing import Any

from board_contract import Board


ROOT = Path(__file__).resolve().parents[1]
SCHEMA_VERSION = 2
ATTESTATION_TYPE = "hackylens-firmware-build"
FULL_BUILD_PROFILE = "hackylens-full"
FEATURE_BUILD_PROFILE = "hackylens-feature-modified"
FAULT_BUILD_PROFILE = "hackylens-wdt-fault-injection"
FULL_APP_IDS = frozenset({
    "terminal",
    "camera",
    "qr-camera",
    "face-detect",
    "apriltag",
    "object-detect",
    "files",
    "buttons",
    "pong",
    "settings",
    "sleep",
    "micropython",
})
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
ROOT_FIELDS = {
    "schema",
    "attestation_type",
    "firmware_version",
    "board_id",
    "platform_id",
    "runtime_profile",
    "target",
    "build_profile",
    "composition",
    "wdt_fault_injection",
    "release_qualified",
    "image",
}
COMPOSITION_FIELDS = {
    "enabled_apps", "disabled_apps", "disabled_capabilities",
    "capabilities_sha256", "exclusions",
}
EXCLUSION_FIELDS = {"app", "code", "missing"}
IMAGE_FIELDS = {"size", "sha256"}


class AttestationError(ValueError):
    """An attestation is absent, malformed, non-canonical, or mismatched."""


def canonical_json_bytes(document: Any) -> bytes:
    return (
        json.dumps(
            document,
            ensure_ascii=False,
            allow_nan=False,
            sort_keys=True,
            indent=2,
            separators=(",", ": "),
        ).encode("utf-8")
        + b"\n"
    )


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
        raise AttestationError(f"cannot read canonical VERSION: {exc}") from exc
    if not version:
        raise AttestationError("canonical VERSION is empty")
    return version


def _build_profile(
    disabled_apps: set[str], exclusions: list[dict[str, object]],
    disabled_capabilities: set[str],
    wdt_fault_injection: bool,
) -> str:
    if wdt_fault_injection:
        return FAULT_BUILD_PROFILE
    if disabled_apps or exclusions or disabled_capabilities:
        return FEATURE_BUILD_PROFILE
    return FULL_BUILD_PROFILE


def build_document(
    image: Path,
    board: Board,
    *,
    target: str,
    disabled_apps: set[str],
    exclusions: list[dict[str, object]],
    disabled_capabilities: set[str] | None = None,
    capabilities_sha256: str | None = None,
    wdt_fault_injection: bool = False,
) -> dict[str, object]:
    if not image.is_file() or image.stat().st_size <= 0:
        raise AttestationError(f"firmware image is missing or empty: {image}")
    if board.runtime_profile is None:
        raise AttestationError("firmware build attestation requires a runtime board")
    unknown = disabled_apps - FULL_APP_IDS
    if unknown:
        raise AttestationError(
            "unknown disabled app(s): " + ", ".join(sorted(unknown))
        )
    enabled_apps = FULL_APP_IDS - disabled_apps
    disabled_capabilities = disabled_capabilities or set()
    if (not isinstance(capabilities_sha256, str) or
            SHA256_RE.fullmatch(capabilities_sha256) is None):
        raise AttestationError("capabilities_sha256: expected lowercase SHA-256")
    exclusions = sorted(exclusions, key=lambda item: str(item.get("app", "")))
    profile = _build_profile(
        disabled_apps, exclusions, disabled_capabilities, wdt_fault_injection
    )
    release_qualified = (
        target == "full"
        and profile == FULL_BUILD_PROFILE
        and enabled_apps == FULL_APP_IDS
        and not exclusions
        and not disabled_capabilities
        and not wdt_fault_injection
    )
    document: dict[str, object] = {
        "schema": SCHEMA_VERSION,
        "attestation_type": ATTESTATION_TYPE,
        "firmware_version": current_firmware_version(),
        "board_id": board.id,
        "platform_id": board.registry.platform,
        "runtime_profile": board.runtime_profile,
        "target": target,
        "build_profile": profile,
        "composition": {
            "enabled_apps": sorted(enabled_apps),
            "disabled_apps": sorted(disabled_apps),
            "disabled_capabilities": sorted(disabled_capabilities),
            "capabilities_sha256": capabilities_sha256,
            "exclusions": exclusions,
        },
        "wdt_fault_injection": bool(wdt_fault_injection),
        "release_qualified": release_qualified,
        "image": {
            "size": image.stat().st_size,
            "sha256": sha256_file(image),
        },
    }
    # Run the same exact validator used by consumers before persisting evidence.
    validate_document(document, image, board)
    return document


def _exact_object(value: Any, fields: set[str], label: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise AttestationError(f"{label}: expected object")
    unknown = sorted(set(value) - fields)
    missing = sorted(fields - set(value))
    if unknown or missing:
        details: list[str] = []
        if unknown:
            details.append("unknown=" + ",".join(unknown))
        if missing:
            details.append("missing=" + ",".join(missing))
        raise AttestationError(f"{label}: " + "; ".join(details))
    return value


def _string(value: Any, label: str) -> str:
    if not isinstance(value, str) or not value:
        raise AttestationError(f"{label}: expected non-empty string")
    return value


def _string_list(value: Any, label: str) -> list[str]:
    if (
        not isinstance(value, list)
        or any(not isinstance(item, str) or not item for item in value)
        or value != sorted(set(value))
    ):
        raise AttestationError(f"{label}: expected sorted unique string array")
    return value


def validate_document(
    document: Any,
    image: Path,
    board: Board,
    *,
    expected_target: str | None = None,
    require_release_qualified: bool = False,
) -> None:
    document = _exact_object(document, ROOT_FIELDS, "root")
    if type(document["schema"]) is not int or document["schema"] != SCHEMA_VERSION:
        raise AttestationError(f"schema: expected integer {SCHEMA_VERSION}")
    for field in (
        "attestation_type", "firmware_version", "board_id", "platform_id",
        "runtime_profile", "target", "build_profile",
    ):
        _string(document[field], field)
    for field in ("wdt_fault_injection", "release_qualified"):
        if not isinstance(document[field], bool):
            raise AttestationError(f"{field}: expected boolean")
    composition = _exact_object(
        document["composition"], COMPOSITION_FIELDS, "composition"
    )
    enabled = _string_list(composition["enabled_apps"], "composition.enabled_apps")
    disabled = _string_list(composition["disabled_apps"], "composition.disabled_apps")
    disabled_capabilities = _string_list(
        composition["disabled_capabilities"],
        "composition.disabled_capabilities",
    )
    capabilities_sha256 = composition["capabilities_sha256"]
    if (not isinstance(capabilities_sha256, str) or
            SHA256_RE.fullmatch(capabilities_sha256) is None):
        raise AttestationError(
            "composition.capabilities_sha256: expected lowercase SHA-256"
        )
    if set(enabled) & set(disabled) or set(enabled) | set(disabled) != FULL_APP_IDS:
        raise AttestationError(
            "composition: enabled_apps and disabled_apps must exactly partition full apps"
        )
    exclusions = composition["exclusions"]
    if not isinstance(exclusions, list):
        raise AttestationError("composition.exclusions: expected array")
    exclusion_apps: list[str] = []
    for index, value in enumerate(exclusions):
        exclusion = _exact_object(
            value, EXCLUSION_FIELDS, f"composition.exclusions[{index}]"
        )
        app = _string(exclusion["app"], f"composition.exclusions[{index}].app")
        _string(exclusion["code"], f"composition.exclusions[{index}].code")
        _string_list(
            exclusion["missing"], f"composition.exclusions[{index}].missing"
        )
        if app not in disabled:
            raise AttestationError(
                f"composition.exclusions[{index}].app: expected disabled app"
            )
        exclusion_apps.append(app)
    if exclusion_apps != sorted(set(exclusion_apps)):
        raise AttestationError("composition.exclusions: expected app-sorted unique entries")

    image_identity = _exact_object(document["image"], IMAGE_FIELDS, "image")
    size = image_identity["size"]
    if isinstance(size, bool) or not isinstance(size, int) or size <= 0:
        raise AttestationError("image.size: expected positive integer")
    digest = image_identity["sha256"]
    if not isinstance(digest, str) or SHA256_RE.fullmatch(digest) is None:
        raise AttestationError("image.sha256: expected lowercase SHA-256")
    if not image.is_file():
        raise AttestationError(f"firmware image not found: {image}")

    expected_profile = _build_profile(
        set(disabled), exclusions, set(disabled_capabilities),
        document["wdt_fault_injection"]
    )
    expected_release = (
        document["target"] == "full"
        and expected_profile == FULL_BUILD_PROFILE
        and set(enabled) == FULL_APP_IDS
        and not exclusions
        and not disabled_capabilities
        and not document["wdt_fault_injection"]
    )
    expected = {
        "attestation_type": ATTESTATION_TYPE,
        "firmware_version": current_firmware_version(),
        "board_id": board.id,
        "platform_id": board.registry.platform,
        "runtime_profile": board.runtime_profile,
        "build_profile": expected_profile,
        "release_qualified": expected_release,
        "image.size": image.stat().st_size,
        "image.sha256": sha256_file(image),
    }
    actual = {
        "attestation_type": document["attestation_type"],
        "firmware_version": document["firmware_version"],
        "board_id": document["board_id"],
        "platform_id": document["platform_id"],
        "runtime_profile": document["runtime_profile"],
        "build_profile": document["build_profile"],
        "release_qualified": document["release_qualified"],
        "image.size": size,
        "image.sha256": digest,
    }
    mismatches = [
        f"{field}: expected {expected[field]!r}, got {actual[field]!r}"
        for field in expected
        if actual[field] != expected[field]
    ]
    if expected_target is not None and document["target"] != expected_target:
        mismatches.append(
            f"target: expected {expected_target!r}, got {document['target']!r}"
        )
    if require_release_qualified and not document["release_qualified"]:
        mismatches.append("release_qualified: a full release build is required")
    if mismatches:
        raise AttestationError("build attestation mismatch: " + "; ".join(mismatches))


def read_and_validate(
    path: Path,
    image: Path,
    board: Board,
    *,
    expected_target: str | None = None,
    require_release_qualified: bool = False,
) -> dict[str, Any]:
    try:
        encoded = path.read_bytes()
        document = json.loads(encoded.decode("utf-8"))
        canonical = canonical_json_bytes(document)
    except (OSError, UnicodeDecodeError, json.JSONDecodeError, ValueError) as exc:
        raise AttestationError(f"cannot read build attestation {path}: {exc}") from exc
    if encoded != canonical:
        raise AttestationError("build attestation is not exact canonical JSON")
    validate_document(
        document,
        image,
        board,
        expected_target=expected_target,
        require_release_qualified=require_release_qualified,
    )
    return document


def write(
    path: Path,
    image: Path,
    board: Board,
    *,
    target: str,
    disabled_apps: set[str],
    exclusions: list[dict[str, object]],
    disabled_capabilities: set[str] | None = None,
    capabilities_sha256: str | None = None,
    wdt_fault_injection: bool = False,
) -> dict[str, object]:
    if path.resolve() == image.resolve():
        raise AttestationError("build attestation path must differ from firmware image")
    document = build_document(
        image,
        board,
        target=target,
        disabled_apps=disabled_apps,
        exclusions=exclusions,
        disabled_capabilities=disabled_capabilities,
        capabilities_sha256=capabilities_sha256,
        wdt_fault_injection=wdt_fault_injection,
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(canonical_json_bytes(document))
    return document
