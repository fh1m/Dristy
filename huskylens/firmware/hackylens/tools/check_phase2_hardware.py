#!/usr/bin/env python3
"""Validate Phase 2.13 SEN0305 physical acceptance evidence."""

from __future__ import annotations

import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import re
import sys
from typing import Any, Iterable


ROOT = Path(__file__).resolve().parents[1]
TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import check_phase2_resources
import firmware_attestation


DEFAULT_EVIDENCE = ROOT / "docs" / "evidence" / "phase2-hardware-smoke.json"
HARDWARE_ARTIFACT_ROOT = Path("docs/evidence/phase2-hardware")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")
SEMVER_RE = re.compile(
    r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)"
    r"(?:-[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?"
    r"(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$"
)
WORKFLOW_URL_RE = re.compile(
    r"^https://github\.com/aztechell/hackylens/actions/runs/(\d+)$"
)

ROOT_FIELDS = {
    "accepted", "artifacts", "board", "candidate", "excluded_boards",
    "fixture", "operator", "phase", "recorded_at", "results", "schema",
}
CANDIDATE_FIELDS = {
    "firmware", "result", "source_commit", "source_manifest_sha256",
    "toolchain", "workflow",
}
FIRMWARE_FIELDS = {
    "attestation_sha256", "capabilities_sha256", "composition_sha256",
    "image_bytes", "image_sha256", "version",
}
TOOLCHAIN_FIELDS = {
    "archive_sha256", "host_cmake", "host_python",
    "kendryte_standalone_sdk_revision", "kendryte_toolchain",
}
RESULT_IDS = {
    "buttons", "display", "external_service_restore", "i2c", "lights",
    "regression", "time", "uart",
}
DISPLAY_COVERAGE = [
    "menu",
    "native-views",
    "camera-full-frame",
    "files-full-frame",
    "pong-dirty-frames",
    "micropython-overlay",
    "micropython-cancel",
    "micropython-retry",
    "micropython-cleanup",
]
LIGHTS_COVERAGE = ["backlight", "illumination", "rgb"]
REGRESSION_COVERAGE = [
    "boot", "camera", "sd-read", "sd-write", "sd-delete", "files-decode",
    "frame-pool", "settings", "sleep", "hmpy",
]
LOGICAL_BUTTONS = ["left", "ok", "right", "back"]
IMPACT_RESULT_IDS = {
    "boot", "buttons", "camera-display", "external-service", "files",
    "i2c", "lights", "menu", "micropython", "pong", "sleep", "time",
    "uart",
}


class HardwareEvidenceError(RuntimeError):
    """Phase 2.13 evidence is incomplete, inconsistent, or stale."""


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def exact_fields(value: Any, fields: set[str], label: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise HardwareEvidenceError(f"{label} must be an object")
    actual = set(value)
    if actual != fields:
        missing = sorted(fields - actual)
        unknown = sorted(actual - fields)
        raise HardwareEvidenceError(
            f"{label} has missing or unknown fields "
            f"(missing={missing}, unknown={unknown})"
        )
    return value


def require_string(value: Any, label: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise HardwareEvidenceError(f"{label} must be a non-empty string")
    return value


def require_bool(value: Any, expected: bool, label: str) -> None:
    if value is not expected:
        raise HardwareEvidenceError(f"{label} must be {str(expected).lower()}")


def require_int(value: Any, label: str, *, minimum: int = 0) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        raise HardwareEvidenceError(f"{label} must be an integer >= {minimum}")
    return value


def require_number(value: Any, label: str, *, minimum: float = 0.0) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise HardwareEvidenceError(f"{label} must be numeric")
    result = float(value)
    if result < minimum:
        raise HardwareEvidenceError(f"{label} must be >= {minimum}")
    return result


def require_sha256(value: Any, label: str) -> str:
    if not isinstance(value, str) or not SHA256_RE.fullmatch(value):
        raise HardwareEvidenceError(f"{label} must be a lowercase SHA-256")
    return value


def repository_path(
    root: Path,
    value: Any,
    label: str,
    *,
    required_parent: Path | None = None,
) -> Path:
    raw = require_string(value, label)
    relative = Path(raw)
    if relative.is_absolute():
        raise HardwareEvidenceError(f"{label} must be repository-relative")
    candidate = (root / relative).resolve()
    try:
        candidate.relative_to(root.resolve())
    except ValueError as exc:
        raise HardwareEvidenceError(f"{label} escapes the repository") from exc
    if required_parent is not None:
        parent = (root / required_parent).resolve()
        try:
            candidate.relative_to(parent)
        except ValueError as exc:
            raise HardwareEvidenceError(
                f"{label} must be under {required_parent.as_posix()}"
            ) from exc
    if not candidate.is_file():
        raise HardwareEvidenceError(f"{label} does not exist: {raw}")
    return candidate


def read_canonical_json(path: Path, label: str) -> dict[str, Any]:
    try:
        encoded = path.read_bytes()
        document = json.loads(encoded.decode("utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise HardwareEvidenceError(f"cannot read {label}: {exc}") from exc
    if not isinstance(document, dict):
        raise HardwareEvidenceError(f"{label} root must be an object")
    if encoded != firmware_attestation.canonical_json_bytes(document):
        raise HardwareEvidenceError(f"{label} is not exact canonical JSON")
    return document


def validate_timestamp(value: Any) -> None:
    text = require_string(value, "recorded_at")
    if not text.endswith("Z"):
        raise HardwareEvidenceError("recorded_at must be UTC and end in Z")
    try:
        datetime.fromisoformat(text[:-1] + "+00:00")
    except ValueError as exc:
        raise HardwareEvidenceError("recorded_at must be ISO-8601 UTC") from exc


def validate_timing(value: Any, label: str) -> None:
    timing = exact_fields(value, {"max", "min", "p99", "samples"}, label)
    minimum = require_number(timing["min"], f"{label}.min")
    p99 = require_number(timing["p99"], f"{label}.p99")
    maximum = require_number(timing["max"], f"{label}.max")
    require_int(timing["samples"], f"{label}.samples", minimum=1)
    if not minimum <= p99 <= maximum:
        raise HardwareEvidenceError(f"{label} must satisfy min <= p99 <= max")


def validate_artifacts(
    root: Path, artifacts_value: Any
) -> dict[str, str]:
    if not isinstance(artifacts_value, list) or not artifacts_value:
        raise HardwareEvidenceError("artifacts must be a non-empty array")
    artifacts: dict[str, str] = {}
    for index, value in enumerate(artifacts_value):
        label = f"artifacts[{index}]"
        artifact = exact_fields(value, {"kind", "path", "sha256"}, label)
        kind = require_string(artifact["kind"], f"{label}.kind")
        raw_path = require_string(artifact["path"], f"{label}.path")
        if raw_path in artifacts:
            raise HardwareEvidenceError(f"duplicate artifact path: {raw_path}")
        path = repository_path(
            root,
            raw_path,
            f"{label}.path",
            required_parent=HARDWARE_ARTIFACT_ROOT,
        )
        expected = require_sha256(artifact["sha256"], f"{label}.sha256")
        if sha256(path) != expected:
            raise HardwareEvidenceError(f"{label} content hash mismatch")
        artifacts[raw_path] = kind
    required_kinds = {"fixture-schematic", "raw-log", "timing-log"}
    missing_kinds = required_kinds - set(artifacts.values())
    if missing_kinds:
        raise HardwareEvidenceError(
            f"artifacts omit required kinds: {sorted(missing_kinds)}"
        )
    return artifacts


def validate_artifact_references(
    value: Any, label: str, declared_artifacts: set[str]
) -> None:
    if not isinstance(value, list) or not value:
        raise HardwareEvidenceError(f"{label} must be a non-empty array")
    if any(not isinstance(item, str) for item in value):
        raise HardwareEvidenceError(f"{label} entries must be paths")
    if len(value) != len(set(value)):
        raise HardwareEvidenceError(f"{label} contains duplicate paths")
    unknown = set(value) - declared_artifacts
    if unknown:
        raise HardwareEvidenceError(
            f"{label} references undeclared artifacts: {sorted(unknown)}"
        )


def validate_candidate(root: Path, value: Any) -> dict[str, Any]:
    candidate = exact_fields(value, CANDIDATE_FIELDS, "candidate")
    source_commit = candidate["source_commit"]
    if not isinstance(source_commit, str) or not COMMIT_RE.fullmatch(source_commit):
        raise HardwareEvidenceError("candidate.source_commit must be a full commit SHA")
    source_manifest = require_sha256(
        candidate["source_manifest_sha256"],
        "candidate.source_manifest_sha256",
    )

    workflow = exact_fields(
        candidate["workflow"], {"run_id", "url"}, "candidate.workflow"
    )
    run_id = require_int(workflow["run_id"], "candidate.workflow.run_id", minimum=1)
    workflow_url = require_string(workflow["url"], "candidate.workflow.url")
    match = WORKFLOW_URL_RE.fullmatch(workflow_url)
    if not match or int(match.group(1)) != run_id:
        raise HardwareEvidenceError("candidate workflow URL/run ID mismatch")

    result_link = exact_fields(
        candidate["result"], {"path", "sha256"}, "candidate.result"
    )
    result_path = repository_path(
        root,
        result_link["path"],
        "candidate.result.path",
        required_parent=Path("docs/evidence"),
    )
    if result_path.name != "phase2-candidate-result.json":
        raise HardwareEvidenceError(
            "candidate.result must name immutable phase2-candidate-result.json"
        )
    expected_result_sha = require_sha256(
        result_link["sha256"], "candidate.result.sha256"
    )
    if sha256(result_path) != expected_result_sha:
        raise HardwareEvidenceError("candidate result content hash mismatch")
    result = read_canonical_json(result_path, "Phase 2 candidate result")
    try:
        check_phase2_resources.validate_result_document(result)
    except RuntimeError as exc:
        raise HardwareEvidenceError(f"candidate result is invalid: {exc}") from exc
    if result.get("accepted") is not True or result.get("gate") != "pre-hardware":
        raise HardwareEvidenceError("candidate automated result did not pass")
    if result.get("physical_scope") != {
        "claims": [], "deferred_to": "Phase 2.13", "status": "not-run",
    }:
        raise HardwareEvidenceError(
            "candidate result must not contain inherited hardware claims"
        )
    if result.get("source", {}).get("manifest_sha256") != source_manifest:
        raise HardwareEvidenceError("candidate source manifest mismatch")

    profiles = result.get("builds", {}).get("profiles", {})
    full = profiles.get("full") if isinstance(profiles, dict) else None
    if not isinstance(full, dict):
        raise HardwareEvidenceError("candidate result lacks the full profile")
    firmware = exact_fields(candidate["firmware"], FIRMWARE_FIELDS, "candidate.firmware")
    version = require_string(firmware["version"], "candidate.firmware.version")
    if not SEMVER_RE.fullmatch(version):
        raise HardwareEvidenceError("candidate firmware version is not SemVer")
    version_path = root / "VERSION"
    if not version_path.is_file() or version_path.read_text(encoding="utf-8").strip() != version:
        raise HardwareEvidenceError(
            "candidate firmware version differs from the repository VERSION"
        )
    expected = {
        "attestation_sha256": full.get("attestation_sha256"),
        "capabilities_sha256": full.get("capabilities_sha256"),
        "composition_sha256": full.get("composition_sha256"),
        "image_bytes": full.get("image", {}).get("raw_bytes"),
        "image_sha256": full.get("image", {}).get("sha256"),
    }
    for field, expected_value in expected.items():
        actual = firmware[field]
        if field == "image_bytes":
            require_int(actual, f"candidate.firmware.{field}", minimum=1)
        else:
            require_sha256(actual, f"candidate.firmware.{field}")
        if actual != expected_value:
            raise HardwareEvidenceError(
                f"candidate firmware {field} does not match the full profile"
            )
    if full.get("board") != "huskylens-sen0305" or \
            full.get("release_qualified") is not True:
        raise HardwareEvidenceError("candidate full profile is not SEN0305 release-qualified")

    toolchain = exact_fields(
        candidate["toolchain"], TOOLCHAIN_FIELDS, "candidate.toolchain"
    )
    require_sha256(toolchain["archive_sha256"], "candidate.toolchain.archive_sha256")
    sdk_revision = toolchain["kendryte_standalone_sdk_revision"]
    if not isinstance(sdk_revision, str) or not COMMIT_RE.fullmatch(sdk_revision):
        raise HardwareEvidenceError(
            "candidate.toolchain.kendryte_standalone_sdk_revision must be a full commit SHA"
        )
    for field in ("host_cmake", "host_python", "kendryte_toolchain"):
        require_string(toolchain[field], f"candidate.toolchain.{field}")
    baseline_path = root / "docs" / "evidence" / "phase2-baseline.json"
    baseline = read_canonical_json(baseline_path, "Phase 2 baseline")
    baseline_toolchain = baseline.get("toolchain", {})
    pinned_fields = (
        "archive_sha256", "kendryte_standalone_sdk_revision", "kendryte_toolchain",
    )
    if any(toolchain[field] != baseline_toolchain.get(field) for field in pinned_fields):
        raise HardwareEvidenceError("candidate pinned toolchain differs from Phase 2 baseline")
    return result


def validate_fixture(
    root: Path,
    value: Any,
    declared_artifacts: dict[str, str],
) -> int:
    fixture = exact_fields(
        value, {"i2c", "id", "schematic", "uart"}, "fixture"
    )
    require_string(fixture["id"], "fixture.id")
    schematic = exact_fields(
        fixture["schematic"], {"path", "sha256"}, "fixture.schematic"
    )
    schematic_path = require_string(schematic["path"], "fixture.schematic.path")
    if schematic_path not in declared_artifacts:
        raise HardwareEvidenceError("fixture schematic is not a declared artifact")
    if declared_artifacts[schematic_path] != "fixture-schematic":
        raise HardwareEvidenceError("fixture schematic artifact has the wrong kind")
    actual_path = repository_path(
        root,
        schematic_path,
        "fixture.schematic.path",
        required_parent=HARDWARE_ARTIFACT_ROOT,
    )
    schematic_sha = require_sha256(schematic["sha256"], "fixture.schematic.sha256")
    if sha256(actual_path) != schematic_sha:
        raise HardwareEvidenceError("fixture schematic hash mismatch")

    uart = exact_fields(
        fixture["uart"],
        {"connector", "loopback", "rx_io", "tx_io"},
        "fixture.uart",
    )
    if uart != {
        "connector": "external-four-pin",
        "loopback": True,
        "rx_io": 34,
        "tx_io": 35,
    }:
        raise HardwareEvidenceError("fixture UART routing/loopback is not canonical")

    i2c = exact_fields(
        fixture["i2c"],
        {"connector", "scl_io", "sda_io", "target_address_7bit", "target_identity"},
        "fixture.i2c",
    )
    if i2c["connector"] != "external-four-pin" or \
            i2c["scl_io"] != 34 or i2c["sda_io"] != 35:
        raise HardwareEvidenceError("fixture I2C routing is not canonical")
    address = require_int(
        i2c["target_address_7bit"],
        "fixture.i2c.target_address_7bit",
        minimum=1,
    )
    if address > 0x7F:
        raise HardwareEvidenceError("fixture I2C address is not 7-bit")
    require_string(i2c["target_identity"], "fixture.i2c.target_identity")
    return address


def require_pass(value: dict[str, Any], label: str) -> None:
    if value.get("status") != "pass":
        raise HardwareEvidenceError(f"{label}.status must be pass")


def validate_results(
    value: Any,
    declared_artifacts: set[str],
    fixture_i2c_address: int,
) -> None:
    results = exact_fields(value, RESULT_IDS, "results")

    buttons = exact_fields(
        results["buttons"],
        {
            "artifact_paths", "debounce_latency_us", "event_latency_us",
            "logical_buttons", "press_release_hold_passed",
            "repeated_edges_during_hold", "status",
        },
        "results.buttons",
    )
    require_pass(buttons, "results.buttons")
    if buttons["logical_buttons"] != LOGICAL_BUTTONS:
        raise HardwareEvidenceError("every canonical logical button must be exercised")
    require_bool(
        buttons["press_release_hold_passed"], True,
        "results.buttons.press_release_hold_passed",
    )
    if buttons["repeated_edges_during_hold"] != 0:
        raise HardwareEvidenceError("button hold produced repeated edges")
    validate_timing(buttons["debounce_latency_us"], "results.buttons.debounce_latency_us")
    validate_timing(buttons["event_latency_us"], "results.buttons.event_latency_us")
    validate_artifact_references(
        buttons["artifact_paths"], "results.buttons.artifact_paths", declared_artifacts
    )

    display = exact_fields(
        results["display"],
        {
            "artifact_paths", "coverage", "full_present_us",
            "matched_workload_regression_percent", "status",
        },
        "results.display",
    )
    require_pass(display, "results.display")
    if display["coverage"] != DISPLAY_COVERAGE:
        raise HardwareEvidenceError("display physical coverage is incomplete")
    validate_timing(display["full_present_us"], "results.display.full_present_us")
    if float(display["full_present_us"]["max"]) > 500_000:
        raise HardwareEvidenceError("display full present exceeded 500 ms")
    regression = require_number(
        display["matched_workload_regression_percent"],
        "results.display.matched_workload_regression_percent",
    )
    if regression > 10.0:
        raise HardwareEvidenceError("display matched-workload regression exceeded 10%")
    validate_artifact_references(
        display["artifact_paths"], "results.display.artifact_paths", declared_artifacts
    )

    uart = exact_fields(
        results["uart"],
        {
            "artifact_paths", "baud", "late_bytes_after_cancel",
            "loopback_rx_bytes", "payload_bytes", "status", "wire_drain_observed",
        },
        "results.uart",
    )
    require_pass(uart, "results.uart")
    require_int(uart["baud"], "results.uart.baud", minimum=1200)
    payload_bytes = require_int(
        uart["payload_bytes"], "results.uart.payload_bytes", minimum=1
    )
    if uart["loopback_rx_bytes"] != payload_bytes:
        raise HardwareEvidenceError("UART loopback did not return the exact payload length")
    require_bool(
        uart["wire_drain_observed"], True, "results.uart.wire_drain_observed"
    )
    if uart["late_bytes_after_cancel"] != 0:
        raise HardwareEvidenceError("UART cancellation produced late bytes")
    validate_artifact_references(
        uart["artifact_paths"], "results.uart.artifact_paths", declared_artifacts
    )

    i2c = exact_fields(
        results["i2c"],
        {
            "artifact_paths", "controller_read_passed", "controller_write_passed",
            "error_recovery_passed", "mode_switch_passed", "status",
            "target_address_7bit",
        },
        "results.i2c",
    )
    require_pass(i2c, "results.i2c")
    if i2c["target_address_7bit"] != fixture_i2c_address:
        raise HardwareEvidenceError("I2C result target differs from the fixture")
    for field in (
        "controller_read_passed", "controller_write_passed",
        "error_recovery_passed", "mode_switch_passed",
    ):
        require_bool(i2c[field], True, f"results.i2c.{field}")
    validate_artifact_references(
        i2c["artifact_paths"], "results.i2c.artifact_paths", declared_artifacts
    )

    restore = exact_fields(
        results["external_service_restore"],
        {"artifact_paths", "configured_transport", "hmpy_round_trips", "status"},
        "results.external_service_restore",
    )
    require_pass(restore, "results.external_service_restore")
    if restore["configured_transport"] not in {"uart", "i2c"}:
        raise HardwareEvidenceError("restored external transport is invalid")
    require_int(
        restore["hmpy_round_trips"],
        "results.external_service_restore.hmpy_round_trips",
        minimum=1,
    )
    validate_artifact_references(
        restore["artifact_paths"],
        "results.external_service_restore.artifact_paths",
        declared_artifacts,
    )

    lights = exact_fields(
        results["lights"],
        {"artifact_paths", "cleanup_safe_off", "coverage", "persisted_restore", "status"},
        "results.lights",
    )
    require_pass(lights, "results.lights")
    if lights["coverage"] != LIGHTS_COVERAGE:
        raise HardwareEvidenceError("lights physical coverage is incomplete")
    require_bool(lights["cleanup_safe_off"], True, "results.lights.cleanup_safe_off")
    require_bool(lights["persisted_restore"], True, "results.lights.persisted_restore")
    validate_artifact_references(
        lights["artifact_paths"], "results.lights.artifact_paths", declared_artifacts
    )

    timing = exact_fields(
        results["time"],
        {
            "artifact_paths", "cancel_latency_us", "read_overhead_us",
            "sleep_latency_us", "status",
        },
        "results.time",
    )
    require_pass(timing, "results.time")
    for field in ("cancel_latency_us", "read_overhead_us", "sleep_latency_us"):
        validate_timing(timing[field], f"results.time.{field}")
    validate_artifact_references(
        timing["artifact_paths"], "results.time.artifact_paths", declared_artifacts
    )

    regression_result = exact_fields(
        results["regression"],
        {"artifact_paths", "coverage", "status"},
        "results.regression",
    )
    require_pass(regression_result, "results.regression")
    if regression_result["coverage"] != REGRESSION_COVERAGE:
        raise HardwareEvidenceError("physical regression coverage is incomplete")
    validate_artifact_references(
        regression_result["artifact_paths"],
        "results.regression.artifact_paths",
        declared_artifacts,
    )


def validate_impact_document(
    document: dict[str, Any], root: Path
) -> dict[str, Any]:
    evidence = exact_fields(
        document,
        {
            "accepted", "board", "current_image", "excluded_boards",
            "impact_review", "limitations", "observations", "operator",
            "phase", "policy", "recorded_at", "schema",
        },
        "hardware evidence",
    )
    if evidence["schema"] != 2 or evidence["phase"] != "2.13":
        raise HardwareEvidenceError("hardware evidence schema/phase mismatch")
    require_bool(evidence["accepted"], True, "accepted")
    if evidence["policy"] != "impact-based-owner-acceptance":
        raise HardwareEvidenceError("hardware evidence policy mismatch")
    validate_timestamp(evidence["recorded_at"])
    if evidence["excluded_boards"] != ["sipeed-maix-cube"]:
        raise HardwareEvidenceError("Maix Cube must remain explicitly unqualified")

    board = exact_fields(evidence["board"], {"id", "revision", "serial"}, "board")
    if board["id"] != "huskylens-sen0305":
        raise HardwareEvidenceError("only huskylens-sen0305 may be qualified")
    require_string(board["revision"], "board.revision")
    require_string(board["serial"], "board.serial")
    operator = exact_fields(evidence["operator"], {"identity"}, "operator")
    require_string(operator["identity"], "operator.identity")

    image = exact_fields(
        evidence["current_image"],
        {
            "boot_sanity", "image_bytes", "image_sha256",
            "source_manifest_sha256", "version",
        },
        "current_image",
    )
    require_bool(image["boot_sanity"], True, "current_image.boot_sanity")
    require_int(image["image_bytes"], "current_image.image_bytes", minimum=1)
    require_sha256(image["image_sha256"], "current_image.image_sha256")
    require_sha256(
        image["source_manifest_sha256"], "current_image.source_manifest_sha256"
    )
    version = require_string(image["version"], "current_image.version")
    version_path = root / "VERSION"
    if not version_path.is_file() or \
            version_path.read_text(encoding="utf-8").strip() != version:
        raise HardwareEvidenceError("current image version differs from VERSION")

    observations = evidence["observations"]
    if not isinstance(observations, list):
        raise HardwareEvidenceError("observations must be an array")
    observed_ids: set[str] = set()
    for index, value in enumerate(observations):
        label = f"observations[{index}]"
        observation = exact_fields(
            value, {"id", "method", "result", "status"}, label
        )
        identifier = require_string(observation["id"], f"{label}.id")
        if identifier in observed_ids:
            raise HardwareEvidenceError(f"duplicate observation: {identifier}")
        observed_ids.add(identifier)
        if observation["status"] != "pass":
            raise HardwareEvidenceError(f"{label}.status must be pass")
        require_string(observation["method"], f"{label}.method")
        require_string(observation["result"], f"{label}.result")
    if observed_ids != IMPACT_RESULT_IDS:
        raise HardwareEvidenceError("impact-based physical coverage is incomplete")

    review = exact_fields(
        evidence["impact_review"],
        {
            "broader_retest_required", "carried_forward", "changed_areas",
            "provider_hal_routing_changed", "retested_areas",
        },
        "impact_review",
    )
    require_bool(
        review["provider_hal_routing_changed"], False,
        "impact_review.provider_hal_routing_changed",
    )
    require_bool(
        review["broader_retest_required"], False,
        "impact_review.broader_retest_required",
    )
    for field in ("changed_areas", "retested_areas", "carried_forward"):
        values = review[field]
        if not isinstance(values, list) or not values or \
                any(not isinstance(item, str) or not item for item in values):
            raise HardwareEvidenceError(f"impact_review.{field} must be strings")
        if len(values) != len(set(values)):
            raise HardwareEvidenceError(f"impact_review.{field} has duplicates")
    if not set(review["changed_areas"]) <= set(review["retested_areas"]):
        raise HardwareEvidenceError("every changed runtime area must be retested")

    limitations = evidence["limitations"]
    if not isinstance(limitations, list) or not limitations or \
            any(not isinstance(item, str) or not item for item in limitations):
        raise HardwareEvidenceError(
            "limitations must explicitly describe evidence gaps"
        )
    return evidence


def validate_document(
    document: dict[str, Any], root: Path = ROOT
) -> dict[str, Any]:
    root = root.resolve()
    if document.get("schema") == 2:
        return validate_impact_document(document, root)
    evidence = exact_fields(document, ROOT_FIELDS, "hardware evidence")
    if evidence["schema"] != 1 or evidence["phase"] != "2.13":
        raise HardwareEvidenceError("hardware evidence schema/phase mismatch")
    require_bool(evidence["accepted"], True, "accepted")
    validate_timestamp(evidence["recorded_at"])
    if evidence["excluded_boards"] != ["sipeed-maix-cube"]:
        raise HardwareEvidenceError("Maix Cube must remain explicitly unqualified")

    board = exact_fields(evidence["board"], {"id", "revision", "serial"}, "board")
    if board["id"] != "huskylens-sen0305":
        raise HardwareEvidenceError("only huskylens-sen0305 may be qualified")
    require_string(board["revision"], "board.revision")
    require_string(board["serial"], "board.serial")
    operator = exact_fields(evidence["operator"], {"identity"}, "operator")
    require_string(operator["identity"], "operator.identity")

    validate_candidate(root, evidence["candidate"])
    declared_artifacts = validate_artifacts(root, evidence["artifacts"])
    fixture_address = validate_fixture(root, evidence["fixture"], declared_artifacts)
    validate_results(evidence["results"], set(declared_artifacts), fixture_address)
    return evidence


def validate_file(path: Path, root: Path = ROOT) -> dict[str, Any]:
    document = read_canonical_json(path, "Phase 2 hardware evidence")
    return validate_document(document, root)


def main(argv: Iterable[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence", type=Path, default=DEFAULT_EVIDENCE)
    args = parser.parse_args(list(argv) if argv is not None else None)
    try:
        validate_file(args.evidence)
    except (HardwareEvidenceError, OSError, ValueError) as exc:
        print(f"[ERR] Phase 2.13 hardware gate failed: {exc}", file=sys.stderr)
        return 1
    print("[OK] Phase 2.13 SEN0305 hardware evidence gate passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
