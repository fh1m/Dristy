#!/usr/bin/env python3
"""Validate the canonical Phase 2 baseline and optionally reproduce a profile."""

from __future__ import annotations

import argparse
import ast
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess
import sys
from typing import Any

import build_firmware
import check_phase1_resources
from board_contract import load_board
from gen_flash_layout import load_validated


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BASELINE = ROOT / "docs" / "evidence" / "phase2-baseline.json"
PINNED_BASELINE_SHA256 = (
    "d85d5711548973ad8dcb39c89c93a3363a36e3c08d94121d674604c413a929f5"
)
SOURCE_COMMIT = "7183c7ae59008958893c1585ff6cdd96f1fb746b"
SHA1_RE = re.compile(r"^[0-9a-f]{40}$")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")

ROOT_FIELDS = {
    "schema", "acceptance", "baseline", "formulas", "timing_baseline",
    "toolchain",
}
BASELINE_FIELDS = {"board", "commit", "firmware_version", "profiles", "target"}
PROFILE_FIELDS = {
    "attestation", "build_profile", "composition", "disabled_apps", "elf",
    "image", "release_qualified",
}
ARTIFACT_FIELDS = {"path", "sha256"}
IMAGE_FIELDS = {"flash_occupied_bytes", "path", "raw_bytes", "sha256"}
ELF_FIELDS = {
    "bss_bytes", "data_bytes", "file_bytes", "path", "sha256",
    "static_ram_bytes", "text_bytes",
}
ACCEPTANCE_FIELDS = {
    "additional_full_framebuffers_max", "flash_delta_max_bytes",
    "new_background_tasks_queues_or_heap_allocations",
    "static_ram_delta_max_bytes",
}
FORMULA_FIELDS = {"flash_occupied", "static_ram"}
TIMING_FIELDS = {
    "display_overlay_repaint_timeout_us", "hardware_latency_deferred_to",
    "hardware_latency_status", "measurement_scope",
    "micropython_sleep_cancel_poll_max_us", "pong_fixed_step_us",
    "pong_max_catch_up_steps",
}
TOOLCHAIN_FIELDS = {
    "archive_sha256", "kendryte_standalone_sdk_revision",
    "kendryte_toolchain", "operator_recorded_cmake",
    "operator_recorded_python",
}


def canonical_json_bytes(document: Any) -> bytes:
    return (json.dumps(
        document, ensure_ascii=False, indent=2, sort_keys=True,
        separators=(",", ": "),
    ) + "\n").encode("utf-8")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def exact_fields(value: Any, expected: set[str], label: str) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != expected:
        raise RuntimeError(f"{label} has missing or unknown fields")
    return value


def integer(value: Any, label: str, *, minimum: int = 0) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        raise RuntimeError(f"{label} must be an integer >= {minimum}")
    return value


def string(value: Any, label: str) -> str:
    if not isinstance(value, str) or not value:
        raise RuntimeError(f"{label} must be a non-empty string")
    return value


def validate_artifact(value: Any, label: str) -> dict[str, Any]:
    artifact = exact_fields(value, ARTIFACT_FIELDS, label)
    string(artifact["path"], f"{label}.path")
    digest = string(artifact["sha256"], f"{label}.sha256")
    if SHA256_RE.fullmatch(digest) is None:
        raise RuntimeError(f"{label}.sha256 must be lowercase SHA-256")
    return artifact


def validate_profile(value: Any, label: str) -> dict[str, Any]:
    profile = exact_fields(value, PROFILE_FIELDS, label)
    string(profile["build_profile"], f"{label}.build_profile")
    if not isinstance(profile["release_qualified"], bool):
        raise RuntimeError(f"{label}.release_qualified must be boolean")
    disabled = profile["disabled_apps"]
    if not isinstance(disabled, list) or any(not isinstance(item, str) for item in disabled):
        raise RuntimeError(f"{label}.disabled_apps must be a string array")
    if disabled != sorted(set(disabled)):
        raise RuntimeError(f"{label}.disabled_apps must be sorted and unique")

    image = exact_fields(profile["image"], IMAGE_FIELDS, f"{label}.image")
    string(image["path"], f"{label}.image.path")
    if SHA256_RE.fullmatch(string(image["sha256"], f"{label}.image.sha256")) is None:
        raise RuntimeError(f"{label}.image.sha256 must be lowercase SHA-256")
    integer(image["raw_bytes"], f"{label}.image.raw_bytes", minimum=1)
    integer(
        image["flash_occupied_bytes"],
        f"{label}.image.flash_occupied_bytes",
        minimum=1,
    )

    elf = exact_fields(profile["elf"], ELF_FIELDS, f"{label}.elf")
    string(elf["path"], f"{label}.elf.path")
    if SHA256_RE.fullmatch(string(elf["sha256"], f"{label}.elf.sha256")) is None:
        raise RuntimeError(f"{label}.elf.sha256 must be lowercase SHA-256")
    for field in ELF_FIELDS - {"path", "sha256"}:
        integer(elf[field], f"{label}.elf.{field}", minimum=1)
    if elf["static_ram_bytes"] != elf["data_bytes"] + elf["bss_bytes"]:
        raise RuntimeError(f"{label}.elf.static_ram_bytes formula mismatch")

    validate_artifact(profile["composition"], f"{label}.composition")
    validate_artifact(profile["attestation"], f"{label}.attestation")
    return profile


def validate_document(document: Any) -> dict[str, Any]:
    root = exact_fields(document, ROOT_FIELDS, "root")
    if type(root["schema"]) is not int or root["schema"] != 1:
        raise RuntimeError("schema must be integer 1")

    baseline = exact_fields(root["baseline"], BASELINE_FIELDS, "baseline")
    for field in ("board", "commit", "firmware_version", "target"):
        string(baseline[field], f"baseline.{field}")
    if SHA1_RE.fullmatch(baseline["commit"]) is None:
        raise RuntimeError("baseline.commit must be lowercase Git SHA-1")
    if baseline["commit"] != SOURCE_COMMIT:
        raise RuntimeError(f"baseline.commit must be pinned to {SOURCE_COMMIT}")
    if baseline["target"] != "full":
        raise RuntimeError("baseline.target must be 'full'")
    profiles = exact_fields(
        baseline["profiles"], {"full", "micropython-disabled"},
        "baseline.profiles",
    )
    full = validate_profile(profiles["full"], "baseline.profiles.full")
    disabled = validate_profile(
        profiles["micropython-disabled"],
        "baseline.profiles.micropython-disabled",
    )
    if full["disabled_apps"] != [] or not full["release_qualified"]:
        raise RuntimeError("full profile must be complete and release-qualified")
    if disabled["disabled_apps"] != ["micropython"] or disabled["release_qualified"]:
        raise RuntimeError("micropython-disabled profile metadata mismatch")

    acceptance = exact_fields(root["acceptance"], ACCEPTANCE_FIELDS, "acceptance")
    for field in ACCEPTANCE_FIELDS:
        integer(acceptance[field], f"acceptance.{field}")
    if acceptance != {
        "additional_full_framebuffers_max": 0,
        "flash_delta_max_bytes": 40960,
        "new_background_tasks_queues_or_heap_allocations": 0,
        "static_ram_delta_max_bytes": 4096,
    }:
        raise RuntimeError("Phase 2 acceptance budgets do not match the masterplan")

    formulas = exact_fields(root["formulas"], FORMULA_FIELDS, "formulas")
    if formulas != {
        "flash_occupied": "ceil((raw_image_bytes + 37) / erase_size) * erase_size",
        "static_ram": "data_bytes + bss_bytes",
    }:
        raise RuntimeError("baseline formulas are not canonical")

    timing = exact_fields(root["timing_baseline"], TIMING_FIELDS, "timing_baseline")
    if timing != {
        "display_overlay_repaint_timeout_us": 500000,
        "hardware_latency_deferred_to": "Phase 2.13",
        "hardware_latency_status": "not-collected",
        "measurement_scope": "source-declared-bounds",
        "micropython_sleep_cancel_poll_max_us": 5000,
        "pong_fixed_step_us": 20000,
        "pong_max_catch_up_steps": 8,
    }:
        raise RuntimeError("timing baseline does not match source-declared bounds")

    toolchain = exact_fields(root["toolchain"], TOOLCHAIN_FIELDS, "toolchain")
    for field in TOOLCHAIN_FIELDS:
        string(toolchain[field], f"toolchain.{field}")
    if SHA256_RE.fullmatch(toolchain["archive_sha256"]) is None:
        raise RuntimeError("toolchain.archive_sha256 must be lowercase SHA-256")
    return root


def load_baseline(path: Path = DEFAULT_BASELINE) -> dict[str, Any]:
    try:
        encoded = path.read_bytes()
        document = json.loads(encoded.decode("utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise RuntimeError(f"cannot read Phase 2 baseline: {exc}") from exc
    canonical = canonical_json_bytes(document)
    if encoded != canonical:
        raise RuntimeError("Phase 2 baseline is not canonical UTF-8 JSON")
    digest = hashlib.sha256(encoded).hexdigest()
    if digest != PINNED_BASELINE_SHA256:
        raise RuntimeError(
            "Phase 2 baseline digest mismatch: "
            f"expected {PINNED_BASELINE_SHA256}, got {digest}"
        )
    return validate_document(document)


def git_file(commit: str, relative: str, *, root: Path = ROOT) -> str:
    result = subprocess.run(
        ["git", "show", f"{commit}:{relative}"], cwd=root, check=True,
        text=True, capture_output=True,
    )
    return result.stdout


def python_string_constant(source: str, name: str, label: str) -> str:
    tree = ast.parse(source, filename=label)
    for statement in tree.body:
        if not isinstance(statement, (ast.Assign, ast.AnnAssign)):
            continue
        targets = statement.targets if isinstance(statement, ast.Assign) else [statement.target]
        if not any(isinstance(target, ast.Name) and target.id == name for target in targets):
            continue
        value = ast.literal_eval(statement.value)
        if isinstance(value, str) and value:
            return value
    raise RuntimeError(f"{label} does not define literal {name}")


def verify_provenance(document: dict[str, Any], *, root: Path = ROOT) -> None:
    baseline = document["baseline"]
    commit = baseline["commit"]
    subprocess.run(
        ["git", "cat-file", "-e", f"{commit}^{{commit}}"], cwd=root,
        check=True, text=True, capture_output=True,
    )
    ancestor = subprocess.run(
        ["git", "merge-base", "--is-ancestor", commit, "HEAD"], cwd=root,
        text=True, capture_output=True,
    )
    if ancestor.returncode != 0:
        raise RuntimeError("Phase 2 baseline commit is not an ancestor of HEAD")
    if git_file(commit, "VERSION", root=root).strip() != baseline["firmware_version"]:
        raise RuntimeError("baseline firmware version does not match pinned commit")

    bootstrap = git_file(commit, "tools/bootstrap_deps.py", root=root)
    toolchain = document["toolchain"]
    if python_string_constant(bootstrap, "SDK_REVISION", "baseline bootstrap") != toolchain["kendryte_standalone_sdk_revision"]:
        raise RuntimeError("baseline SDK revision mismatch")
    if python_string_constant(bootstrap, "TOOLCHAIN_SHA256", "baseline bootstrap") != toolchain["archive_sha256"]:
        raise RuntimeError("baseline toolchain SHA-256 mismatch")
    toolchain_url = python_string_constant(
        bootstrap, "TOOLCHAIN_URL", "baseline bootstrap"
    )
    if f"/{toolchain['kendryte_toolchain']}/" not in toolchain_url:
        raise RuntimeError("baseline toolchain version mismatch")

    timing = document["timing_baseline"]
    lcd = git_file(commit, "firmware/src/drivers/lcd_st7789.c", root=root)
    pong = git_file(commit, "firmware/src/apps/pong/pong_config.h", root=root)
    bindings = git_file(
        commit, "firmware/src/apps/micropython/micropython_bindings.c", root=root
    )
    expected_patterns = (
        (lcd, rf"LCD_OVERLAY_REPAINT_TIMEOUT_US\s+{timing['display_overlay_repaint_timeout_us']}ULL"),
        (pong, rf"PONG_FIXED_STEP_US\s+{timing['pong_fixed_step_us']}ULL"),
        (pong, rf"PONG_MAX_CATCH_UP_STEPS\s+{timing['pong_max_catch_up_steps']}\b"),
        (bindings, rf"if\(slice\s*>\s*{timing['micropython_sleep_cancel_poll_max_us'] // 1000}U\)"),
    )
    if any(re.search(pattern, source) is None for source, pattern in expected_patterns):
        raise RuntimeError("timing baseline does not match pinned source")

    board = load_board(baseline["board"], root=root)
    flash, _ = load_validated(board.flash_layout_path)
    for name, profile in baseline["profiles"].items():
        image = profile["image"]
        occupied = math.ceil(
            (image["raw_bytes"] + build_firmware.K210_IMAGE_OVERHEAD)
            / flash["erase_size"]
        ) * flash["erase_size"]
        if occupied != image["flash_occupied_bytes"]:
            raise RuntimeError(f"{name} flash occupancy formula mismatch")


def read_json(path: Path, label: str) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise RuntimeError(f"cannot read {label}: {exc}") from exc
    if not isinstance(value, dict):
        raise RuntimeError(f"{label} must be a JSON object")
    return value


def verify_profile_budget(
    document: dict[str, Any],
    profile_name: str,
    *,
    raw_bytes: int,
    data_bytes: int,
    bss_bytes: int,
    erase_size: int,
) -> tuple[int, int]:
    profile = document["baseline"]["profiles"][profile_name]
    acceptance = document["acceptance"]
    occupied = math.ceil(
        (raw_bytes + build_firmware.K210_IMAGE_OVERHEAD) / erase_size
    ) * erase_size
    flash_delta = occupied - profile["image"]["flash_occupied_bytes"]
    static_ram_delta = (
        data_bytes + bss_bytes - profile["elf"]["static_ram_bytes"]
    )
    if flash_delta > acceptance["flash_delta_max_bytes"]:
        raise RuntimeError(
            f"{profile_name} flash delta {flash_delta} exceeds Phase 2 budget "
            f"{acceptance['flash_delta_max_bytes']}"
        )
    if static_ram_delta > acceptance["static_ram_delta_max_bytes"]:
        raise RuntimeError(
            f"{profile_name} static RAM delta {static_ram_delta} exceeds "
            f"Phase 2 budget {acceptance['static_ram_delta_max_bytes']}"
        )
    return flash_delta, static_ram_delta


def verify_profile_artifacts(
    document: dict[str, Any], profile_name: str, *, root: Path = ROOT
) -> tuple[int, int]:
    profile = document["baseline"]["profiles"][profile_name]
    image = root / profile["image"]["path"]
    elf = root / profile["elf"]["path"]
    composition_path = root / profile["composition"]["path"]
    attestation_path = root / profile["attestation"]["path"]
    for path in (image, elf, composition_path, attestation_path):
        if not path.is_file():
            raise RuntimeError(f"profile artifact is missing: {path}")

    image_size = image.stat().st_size
    image_sha256 = sha256(image)
    text_bytes, data_bytes, bss_bytes = check_phase1_resources.read_elf_size(elf)

    composition = read_json(composition_path, f"{profile_name} composition")
    attestation = read_json(attestation_path, f"{profile_name} attestation")
    if composition.get("schema") != 2:
        raise RuntimeError(f"{profile_name} composition schema mismatch")
    capabilities_ref = composition.get("capabilities")
    if not isinstance(capabilities_ref, dict):
        raise RuntimeError(f"{profile_name} capability inventory reference is missing")
    capabilities_path = composition_path.parent / str(capabilities_ref.get("path", ""))
    if not capabilities_path.is_file():
        raise RuntimeError(f"{profile_name} capabilities artifact is missing")
    capabilities_sha256 = sha256(capabilities_path)
    if capabilities_ref.get("sha256") != capabilities_sha256:
        raise RuntimeError(f"{profile_name} capabilities artifact hash mismatch")
    if composition.get("disabled_apps") != profile["disabled_apps"]:
        raise RuntimeError(f"{profile_name} composition disabled apps mismatch")
    if attestation.get("build_profile") != profile["build_profile"]:
        raise RuntimeError(f"{profile_name} attestation build profile mismatch")
    if attestation.get("release_qualified") is not profile["release_qualified"]:
        raise RuntimeError(f"{profile_name} release qualification mismatch")
    if attestation.get("image", {}).get("sha256") != image_sha256:
        raise RuntimeError(f"{profile_name} attestation image hash mismatch")
    if attestation.get("image", {}).get("size") != image_size:
        raise RuntimeError(f"{profile_name} attestation image size mismatch")
    if attestation.get("composition", {}).get("disabled_apps") != \
            composition.get("disabled_apps"):
        raise RuntimeError(f"{profile_name} attestation composition mismatch")
    if attestation.get("composition", {}).get("disabled_capabilities") != \
            composition.get("disabled_capabilities"):
        raise RuntimeError(f"{profile_name} disabled capability mismatch")
    if attestation.get("composition", {}).get("capabilities_sha256") != \
            capabilities_sha256:
        raise RuntimeError(f"{profile_name} attestation capability hash mismatch")

    board = load_board(document["baseline"]["board"], root=root)
    flash, _ = load_validated(board.flash_layout_path)
    return verify_profile_budget(
        document,
        profile_name,
        raw_bytes=image_size,
        data_bytes=data_bytes,
        bss_bytes=bss_bytes,
        erase_size=flash["erase_size"],
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, default=DEFAULT_BASELINE)
    parser.add_argument(
        "--verify-profile", choices=("full", "micropython-disabled")
    )
    args = parser.parse_args(argv)
    try:
        document = load_baseline(args.baseline)
        verify_provenance(document)
        deltas = None
        if args.verify_profile:
            deltas = verify_profile_artifacts(document, args.verify_profile)
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as exc:
        print(f"[ERR] Phase 2 evidence check failed: {exc}", file=sys.stderr)
        return 1
    message = "[OK] Phase 2 baseline evidence is canonical and provenance-checked"
    if args.verify_profile:
        flash_delta, static_ram_delta = deltas
        message += (
            f"; {args.verify_profile} artifacts are attested and within budget "
            f"(flash_delta={flash_delta}, static_ram_delta={static_ram_delta})"
        )
    print(message)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
