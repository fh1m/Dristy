#!/usr/bin/env python3
"""Capture and enforce the Phase 2 pre-hardware build/resource evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess
import sys
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import build_firmware
import check_phase1_resources
import check_phase2_evidence
from board_contract import load_board
from gen_flash_layout import load_validated


BOARD_ID = "huskylens-sen0305"
CONFORMANCE_BOARD_ID = "sipeed-maix-cube"
DEFAULT_BASELINE = ROOT / "docs" / "evidence" / "phase2-baseline.json"
DEFAULT_RESULT = ROOT / "docs" / "evidence" / "phase2-result.json"
RECEIPT_DIR = ROOT / "build" / "phase2-qualification"
EXPECTED_CAPABILITIES = (
    "hackylens.cap.time",
    "hackylens.cap.input",
    "hackylens.cap.display",
    "hackylens.cap.external-link",
    "hackylens.cap.lights",
)
CAPABILITY_SLUGS = {
    capability: capability.rsplit(".", 1)[-1].replace("-", "_")
    for capability in EXPECTED_CAPABILITIES
}
REQUIRED_APP_BY_CAPABILITY = {
    "hackylens.cap.time": "pong",
    "hackylens.cap.input": "pong",
    "hackylens.cap.display": "pong",
    "hackylens.cap.external-link": "micropython",
    "hackylens.cap.lights": "settings",
}
SOURCE_PATHS = (
    ".github/workflows/release.yml",
    "VERSION",
    "boards",
    "firmware",
    "platforms",
    "tests",
    "tools",
    "docs/spec",
)
DIRECT_RESOURCE_NAMES = {
    name.casefold() for name in check_phase1_resources.DIRECT_RUNTIME_CALLS
}


def canonical_json_bytes(value: Any) -> bytes:
    return (json.dumps(
        value, ensure_ascii=False, allow_nan=False, indent=2, sort_keys=True,
        separators=(",", ": "),
    ) + "\n").encode("utf-8")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_json(path: Path, label: str) -> dict[str, Any]:
    try:
        encoded = path.read_bytes()
        value = json.loads(encoded.decode("utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise RuntimeError(f"cannot read {label}: {exc}") from exc
    if not isinstance(value, dict):
        raise RuntimeError(f"{label} must be a JSON object")
    return value


def read_canonical_json(path: Path, label: str) -> dict[str, Any]:
    value = read_json(path, label)
    if path.read_bytes() != canonical_json_bytes(value):
        raise RuntimeError(f"{label} is not canonical UTF-8 JSON")
    return value


def write_receipt(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(canonical_json_bytes(value))


def ensure_tracked_result(path: Path) -> None:
    try:
        relative = path.resolve().relative_to(ROOT.resolve()).as_posix()
    except ValueError as exc:
        raise RuntimeError("Phase 2 result must be inside the repository") from exc
    tracked = subprocess.run(
        ["git", "ls-files", "--error-unmatch", "--", relative], cwd=ROOT,
        text=True, capture_output=True,
    )
    if tracked.returncode != 0:
        raise RuntimeError(f"Phase 2 result is not tracked: {relative}")


def repository_source_identity(*, root: Path = ROOT) -> dict[str, Any]:
    command = [
        "git", "ls-files", "--cached", "--others", "--exclude-standard",
        "--", *SOURCE_PATHS,
    ]
    result = subprocess.run(
        command, cwd=root, check=True, text=True, capture_output=True,
    )
    relative_paths = sorted({
        Path(line).as_posix()
        for line in result.stdout.splitlines()
        if line and "__pycache__" not in Path(line).parts
        and Path(line).suffix.casefold() not in {".pyc", ".pyo"}
    })
    if not relative_paths:
        raise RuntimeError("qualification source manifest is empty")
    for relative in relative_paths:
        if "\n" in relative or not (root / relative).is_file():
            raise RuntimeError(f"qualification source is invalid: {relative!r}")
    hashes = subprocess.run(
        ["git", "hash-object", "--stdin-paths"], cwd=root, check=True,
        text=True, input="\n".join(relative_paths) + "\n", capture_output=True,
    ).stdout.splitlines()
    if len(hashes) != len(relative_paths):
        raise RuntimeError("qualification source hash manifest is incomplete")
    digest = hashlib.sha256()
    for relative, object_id in zip(relative_paths, hashes, strict=True):
        encoded_path = relative.encode("utf-8")
        encoded = object_id.encode("ascii")
        digest.update(len(encoded_path).to_bytes(4, "big"))
        digest.update(encoded_path)
        digest.update(len(encoded).to_bytes(4, "big"))
        digest.update(encoded)
    return {
        "content_normalization": "git-clean-filtered-blob-id",
        "file_count": len(relative_paths),
        "manifest_sha256": digest.hexdigest(),
        "scope": list(SOURCE_PATHS),
    }


def artifact_paths(board_id: str = BOARD_ID) -> dict[str, Path]:
    root = ROOT / "build" / board_id
    return {
        "image": root / "hackylens-full.bin",
        "elf": root / "sdk-full" / "hackylens_full",
        "composition": root / "composition.json",
        "capabilities": root / "capabilities.json",
        "attestation": root / "hackylens-full.attestation.json",
    }


def artifact_measurement(paths: dict[str, Path]) -> dict[str, Any]:
    for label, path in paths.items():
        if not path.is_file():
            raise RuntimeError(f"{label} artifact is missing: {path}")
    text_bytes, data_bytes, bss_bytes = check_phase1_resources.read_elf_size(
        paths["elf"]
    )
    board = load_board(BOARD_ID)
    flash, _ = load_validated(board.flash_layout_path)
    raw_bytes = paths["image"].stat().st_size
    rounded = math.ceil(
        (raw_bytes + build_firmware.K210_IMAGE_OVERHEAD) / flash["erase_size"]
    ) * flash["erase_size"]
    return {
        "attestation_sha256": sha256(paths["attestation"]),
        "capabilities_sha256": sha256(paths["capabilities"]),
        "composition_sha256": sha256(paths["composition"]),
        "elf": {
            "bss_bytes": bss_bytes,
            "data_bytes": data_bytes,
            "sha256": sha256(paths["elf"]),
            "static_ram_bytes": data_bytes + bss_bytes,
            "text_bytes": text_bytes,
        },
        "image": {
            "flash_occupied_bytes": rounded,
            "raw_bytes": raw_bytes,
            "sha256": sha256(paths["image"]),
        },
    }


def capture_profile(
    profile_name: str, baseline: dict[str, Any]
) -> dict[str, Any]:
    flash_delta, static_delta = check_phase2_evidence.verify_profile_artifacts(
        baseline, profile_name
    )
    paths = artifact_paths()
    composition = read_json(paths["composition"], "profile composition")
    capabilities = read_json(paths["capabilities"], "profile capabilities")
    attestation = read_json(paths["attestation"], "profile attestation")
    entry_ids = [item.get("id") for item in capabilities.get("entries", [])]
    if entry_ids != list(EXPECTED_CAPABILITIES):
        raise RuntimeError(f"{profile_name} does not contain the five capabilities")
    measurement = artifact_measurement(paths)
    return {
        "schema": 1,
        "kind": "firmware-profile",
        "profile": profile_name,
        "board": BOARD_ID,
        "build_profile": attestation.get("build_profile"),
        "release_qualified": attestation.get("release_qualified"),
        "disabled_apps": composition.get("disabled_apps"),
        "disabled_capabilities": composition.get("disabled_capabilities"),
        "flash_delta_bytes": flash_delta,
        "static_ram_delta_bytes": static_delta,
        **measurement,
    }


def expected_diagnostic_compositions() -> dict[str, dict[str, Any]]:
    board = load_board(BOARD_ID)
    apps = set(build_firmware.APP_MODULES)
    expected: dict[str, dict[str, Any]] = {}
    for capability in EXPECTED_CAPABILITIES:
        composition = build_firmware.compose_capabilities(
            board, set(), set(), {capability},
            allow_required_consumer_exclusion=True,
        )
        required_app = REQUIRED_APP_BY_CAPABILITY[capability]
        try:
            build_firmware.compose_capabilities(
                board, set(), {required_app}, {capability}
            )
        except RuntimeError as exc:
            required_diagnostic = str(exc)
        else:
            raise RuntimeError(
                f"--require-app did not reject absent {capability} for {required_app}"
            )
        expected[capability] = {
            "outcome": "built",
            "disabled_apps": sorted(composition.disabled_apps),
            "exclusion_codes": sorted({
                str(item["code"]) for item in composition.exclusions
            }),
            "required_consumer_exclusions": list(
                composition.required_consumer_exclusions
            ),
            "optional_fallbacks": list(composition.optional_fallbacks),
            "required_app": required_app,
            "required_app_error": required_diagnostic,
        }
    external = expected["hackylens.cap.external-link"]
    fallback_names = {
        str(item.get("fallback")) for item in external["optional_fallbacks"]
    }
    if "settings" in external["disabled_apps"]:
        raise RuntimeError("optional external-link absence disabled settings")
    if not {
        "hide-external-link-menu", "disable-external-link-service",
    }.issubset(fallback_names):
        raise RuntimeError("external-link optional fallbacks are incomplete")
    return expected


def capture_diagnostic(
    capability: str, expected: dict[str, dict[str, Any]]
) -> dict[str, Any]:
    if capability not in CAPABILITY_SLUGS:
        raise RuntimeError(f"unknown diagnostic capability: {capability}")
    paths = artifact_paths()
    composition = read_json(paths["composition"], "diagnostic composition")
    capabilities = read_json(paths["capabilities"], "diagnostic capabilities")
    attestation = read_json(paths["attestation"], "diagnostic attestation")
    if composition.get("disabled_capabilities") != [capability]:
        raise RuntimeError("diagnostic build did not exclude exactly one capability")
    if composition.get("disabled_apps") != expected[capability]["disabled_apps"]:
        raise RuntimeError("diagnostic disabled-app projection mismatch")
    if composition.get("required_consumer_exclusions") != \
            expected[capability]["required_consumer_exclusions"]:
        raise RuntimeError("diagnostic required-consumer exclusion mismatch")
    if attestation.get("release_qualified") is not False:
        raise RuntimeError("diagnostic build claims release qualification")
    if attestation.get("build_profile") != "hackylens-feature-modified":
        raise RuntimeError("diagnostic build profile is not feature-modified")
    entry_ids = [item.get("id") for item in capabilities.get("entries", [])]
    if capability in entry_ids or len(entry_ids) != len(EXPECTED_CAPABILITIES) - 1:
        raise RuntimeError("diagnostic capability inventory mismatch")
    absences = capabilities.get("absences", [])
    if not any(
        item.get("id") == capability and item.get("code") == "provider-excluded"
        for item in absences
    ):
        raise RuntimeError("diagnostic absence is not explicit provider-excluded")
    return {
        "schema": 1,
        "kind": "capability-absent-diagnostic",
        "board": BOARD_ID,
        "absent_capability": capability,
        "outcome": "built",
        "build_attempted": True,
        "build_profile": attestation.get("build_profile"),
        "release_qualified": False,
        "disabled_apps": composition.get("disabled_apps"),
        "exclusion_codes": sorted({
            str(item.get("code")) for item in composition.get("exclusions", [])
        }),
        "required_consumer_exclusions": composition.get(
            "required_consumer_exclusions"
        ),
        "entry_ids": entry_ids,
        **artifact_measurement(paths),
    }


def capture_conformance() -> dict[str, Any]:
    paths = artifact_paths(CONFORMANCE_BOARD_ID)
    composition_path = paths["composition"]
    capabilities_path = paths["capabilities"]
    composition = read_json(composition_path, "Cube composition")
    capabilities = read_json(capabilities_path, "Cube capabilities")
    entry_ids = [item.get("id") for item in capabilities.get("entries", [])]
    if entry_ids != ["hackylens.cap.time"]:
        raise RuntimeError("Cube compile-conformance inventory is not time-only")
    if capabilities.get("runtime_supported") is not False:
        raise RuntimeError("Cube conformance inventory claims runtime support")
    if capabilities.get("support") != "conformance":
        raise RuntimeError("Cube inventory support is not conformance")
    return {
        "schema": 1,
        "kind": "compile-conformance",
        "board": CONFORMANCE_BOARD_ID,
        "compile_only": True,
        "runtime_supported": False,
        "entry_ids": entry_ids,
        "disabled_apps": composition.get("disabled_apps"),
        "capabilities_sha256": sha256(capabilities_path),
        "composition_sha256": sha256(composition_path),
    }


def _new_direct_resources(baseline_commit: str) -> list[str]:
    findings = check_phase1_resources.added_runtime_objects(baseline_commit)
    direct: list[str] = []
    for finding in findings:
        signature = finding.split(" introduced at ", 1)[0]
        if signature == "cxx:new" or signature.startswith("alias:"):
            direct.append(finding)
            continue
        if not signature.startswith("call:"):
            continue
        basename = signature[5:].lstrip(":").rsplit("::", 1)[-1].casefold()
        if basename in DIRECT_RESOURCE_NAMES:
            direct.append(finding)
    return sorted(direct)


def _source_call_count(snapshot: dict[str, str], name: str) -> int:
    pattern = re.compile(rf"\b{re.escape(name)}\s*\(")
    return sum(len(pattern.findall(source)) for source in snapshot.values())


def _full_framebuffer_expression_count(snapshot: dict[str, str]) -> int:
    pattern = re.compile(
        r"\bLCD_W\s*\*\s*LCD_H\s*\*\s*2U\b|"
        r"\bHK_DISPLAY_REQUIRED_WIDTH\s*\*\s*"
        r"HK_DISPLAY_REQUIRED_HEIGHT\s*\*\s*2U\b"
    )
    return sum(len(pattern.findall(source)) for source in snapshot.values())


def compiler_stack_frame_limit() -> int:
    cache = ROOT / "build" / BOARD_ID / "sdk-full" / "CMakeCache.txt"
    try:
        source = cache.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        raise RuntimeError(f"cannot read firmware compiler flags: {exc}") from exc
    match = re.search(r"-Werror=frame-larger-than=(\d+)", source)
    if match is None:
        raise RuntimeError("firmware build omits the bounded stack-frame guard")
    return int(match.group(1))


def micropython_stack_limit() -> int:
    source_path = ROOT / "firmware/src/services/micropython_runtime.c"
    try:
        source = source_path.read_text(encoding="utf-8")
    except OSError as exc:
        raise RuntimeError(f"cannot read MicroPython stack bound: {exc}") from exc
    match = re.search(
        r"#define\s+MICROPYTHON_STACK_LIMIT_BYTES\s+"
        r"\((\d+)U\s*\*\s*(\d+)U\)",
        source,
    )
    if match is None:
        raise RuntimeError("MicroPython runtime omits its explicit stack bound")
    return int(match.group(1)) * int(match.group(2))


def resource_projection(
    baseline: dict[str, Any], profiles: dict[str, Any]
) -> dict[str, Any]:
    commit = str(baseline["baseline"]["commit"])
    acceptance = baseline["acceptance"]
    direct = _new_direct_resources(commit)
    historical = check_phase1_resources._baseline_source_snapshot(
        commit, root=ROOT
    )
    current = check_phase1_resources._current_source_snapshot(root=ROOT)
    core_delta = (
        _source_call_count(current, "hal_core1_start")
        - _source_call_count(historical, "hal_core1_start")
    )
    framebuffer_delta = (
        _full_framebuffer_expression_count(current)
        - _full_framebuffer_expression_count(historical)
    )
    full = profiles["full"]
    disabled = profiles["micropython-disabled"]
    budgets_pass = (
        full["flash_delta_bytes"] <= acceptance["flash_delta_max_bytes"]
        and disabled["flash_delta_bytes"] <= acceptance["flash_delta_max_bytes"]
        and full["static_ram_delta_bytes"] <= acceptance["static_ram_delta_max_bytes"]
        and disabled["static_ram_delta_bytes"] <= acceptance["static_ram_delta_max_bytes"]
        and not direct
        and core_delta <= 0
        and framebuffer_delta <= 0
    )
    return {
        "accepted": budgets_pass,
        "limits": {
            "additional_full_framebuffers": max(0, framebuffer_delta),
            "flash_delta_max_bytes": acceptance["flash_delta_max_bytes"],
            "new_heap_task_queue_resources": 0,
            "new_cores": 0,
            "static_ram_delta_max_bytes": acceptance["static_ram_delta_max_bytes"],
        },
        "observed": {
            "additional_full_framebuffers": 0,
            "full_flash_delta_bytes": full["flash_delta_bytes"],
            "full_static_ram_delta_bytes": full["static_ram_delta_bytes"],
            "micropython_disabled_flash_delta_bytes": disabled["flash_delta_bytes"],
            "micropython_disabled_static_ram_delta_bytes": disabled["static_ram_delta_bytes"],
            "new_core_call_delta": core_delta,
            "new_direct_heap_task_queue_findings": direct,
            "full_framebuffer_expression_delta": framebuffer_delta,
        },
        "framebuffer_proof": (
            "static RAM growth is capped below one 320x240x2 framebuffer and "
            "new direct heap allocation sites are zero"
        ),
        "relevant_stack": {
            "compiler_stack_frame_limit_bytes": compiler_stack_frame_limit(),
            "measurement_scope": "compiler-guard-and-source-declared-bound",
            "micropython_vm_limit_bytes": micropython_stack_limit(),
        },
    }


def latency_projection(contract_receipt: dict[str, Any]) -> dict[str, Any]:
    sources = {
        "input": (ROOT / "firmware/include/hackylens/capability/input.h").read_text(
            encoding="utf-8"
        ),
        "time": (ROOT / "firmware/include/hackylens/capability/time.h").read_text(
            encoding="utf-8"
        ),
        "display": (ROOT / "platforms/k210/capabilities/display_adapter.c").read_text(
            encoding="utf-8"
        ),
        "external": (ROOT / "platforms/k210/capabilities/external_link_adapter.c").read_text(
            encoding="utf-8"
        ),
        "pong": (ROOT / "firmware/src/apps/pong/pong_view.c").read_text(
            encoding="utf-8"
        ),
    }
    required = (
        ("input", r"HK_INPUT_SAMPLE_INTERVAL_US\s+UINT32_C\(10000\)"),
        ("input", r"HK_INPUT_DEBOUNCE_INTERVAL_US\s+UINT32_C\(20000\)"),
        ("time", r"HK_TIME_CANCEL_PROBE_MAX_US\s+UINT64_C\(5000\)"),
        ("display", r"K210_DISPLAY_MAX_PRESENT_US\s+500000U"),
        ("external", r"K210_EXTERNAL_POLL_BYTES\s+32U"),
    )
    for source_name, pattern in required:
        if re.search(pattern, sources[source_name]) is None:
            raise RuntimeError(f"latency source bound is missing: {pattern}")
    if "pong_view_render_frame" not in sources["pong"] or \
            "hk_display_present" in sources["pong"]:
        raise RuntimeError("Pong incremental frame can reach full display present")
    if contract_receipt.get("passed") is not True:
        raise RuntimeError("contract/host p99 receipt did not pass")
    return {
        "accepted": True,
        "host_registry_validation_p99_limit_us": 100,
        "host_registry_validation_p99_passed": True,
        "input_sample_interval_us": 10000,
        "input_debounce_interval_us": 20000,
        "input_publication_bound": "next-sample",
        "display_present_deadline_us": 500000,
        "external_poll_slice_limit_us": 500,
        "external_poll_slice_bytes": 32,
        "terminal_notification_poll_max_us": 5000,
        "pong_incremental_full_present": False,
        "elapsed_hardware_measurements": {
            "display_full_present": "deferred-to-phase-2.13",
            "display_matched_workload_regression": "deferred-to-phase-2.13",
            "external_poll_slice": "deferred-to-phase-2.13",
            "registry_validation_sen0305_p99": "deferred-to-phase-2.13",
        },
        "measurement_scope": "host-p99-and-source-declared-bounds",
    }


def receipt_path(kind: str, name: str | None = None) -> Path:
    suffix = f"-{name}" if name else ""
    return RECEIPT_DIR / f"{kind}{suffix}.json"


def load_receipts() -> tuple[
    dict[str, Any], dict[str, dict[str, Any]], dict[str, Any], dict[str, Any]
]:
    contracts = read_canonical_json(
        receipt_path("contracts"), "contract receipt"
    )
    profiles = {
        name: read_canonical_json(
            receipt_path("profile", name), f"{name} profile receipt"
        )
        for name in ("full", "micropython-disabled")
    }
    diagnostics = {
        capability: read_canonical_json(
            receipt_path("diagnostic", CAPABILITY_SLUGS[capability]),
            f"{capability} diagnostic receipt",
        )
        for capability in EXPECTED_CAPABILITIES
    }
    conformance = read_canonical_json(
        receipt_path("conformance"), "conformance receipt"
    )
    return profiles, diagnostics, conformance, contracts


def build_result(baseline: dict[str, Any]) -> dict[str, Any]:
    profiles, diagnostics, conformance, contracts = load_receipts()
    expected = expected_diagnostic_compositions()
    for capability, receipt in diagnostics.items():
        if receipt.get("absent_capability") != capability:
            raise RuntimeError(f"stale diagnostic receipt for {capability}")
        if receipt.get("outcome") != expected[capability]["outcome"]:
            raise RuntimeError(f"diagnostic receipt outcome drift for {capability}")
        if expected[capability]["outcome"] == "built" and \
                receipt.get("disabled_apps") != expected[capability]["disabled_apps"]:
            raise RuntimeError(f"diagnostic receipt composition drift for {capability}")
        if receipt.get("required_consumer_exclusions") != \
                expected[capability]["required_consumer_exclusions"]:
            raise RuntimeError(
                f"diagnostic required-consumer drift for {capability}"
            )
    if contracts.get("passed") is not True:
        raise RuntimeError("contract suite receipt is not passing")
    if conformance.get("compile_only") is not True or \
            conformance.get("runtime_supported") is not False:
        raise RuntimeError("Cube receipt is not compile-conformance-only")
    resources = resource_projection(baseline, profiles)
    latency = latency_projection(contracts)
    accepted = resources["accepted"] and latency["accepted"]
    return {
        "schema": 1,
        "phase": "2.12",
        "gate": "pre-hardware",
        "accepted": accepted,
        "source": repository_source_identity(),
        "contracts": contracts,
        "composition": {
            "capability_ids": list(EXPECTED_CAPABILITIES),
            "diagnostic_expectations": expected,
        },
        "builds": {
            "profiles": profiles,
            "capability_absent_diagnostics": diagnostics,
            "cube_conformance": conformance,
        },
        "resources": resources,
        "latency": latency,
        "physical_scope": {
            "status": "not-run",
            "deferred_to": "Phase 2.13",
            "claims": [],
        },
    }


def validate_result_document(value: dict[str, Any]) -> dict[str, Any]:
    expected_fields = {
        "accepted", "builds", "composition", "contracts", "gate", "latency",
        "phase", "physical_scope", "resources", "schema", "source",
    }
    if set(value) != expected_fields:
        raise RuntimeError("Phase 2 result root has missing or unknown fields")
    if value.get("schema") != 1 or value.get("phase") != "2.12":
        raise RuntimeError("Phase 2 result identity mismatch")
    if value.get("gate") != "pre-hardware" or value.get("accepted") is not True:
        raise RuntimeError("Phase 2 pre-hardware result is not accepted")
    physical = value.get("physical_scope")
    if physical != {
        "claims": [], "deferred_to": "Phase 2.13", "status": "not-run",
    }:
        raise RuntimeError("rolling result physical scope is not explicitly empty")
    encoded = json.dumps(value, ensure_ascii=False).casefold()
    if "hardware_qualified" in encoded or "hardware-qualified" in encoded:
        raise RuntimeError("rolling result contains a hardware qualification status")
    return value


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_mutually_exclusive_group(required=True)
    actions.add_argument(
        "--capture-profile", choices=("full", "micropython-disabled")
    )
    actions.add_argument(
        "--capture-diagnostic", choices=EXPECTED_CAPABILITIES
    )
    actions.add_argument("--capture-conformance", action="store_true")
    actions.add_argument("--write-result", type=Path, nargs="?", const=DEFAULT_RESULT)
    actions.add_argument("--check-result", type=Path, nargs="?", const=DEFAULT_RESULT)
    parser.add_argument("--baseline", type=Path, default=DEFAULT_BASELINE)
    args = parser.parse_args(argv)
    try:
        baseline = check_phase2_evidence.load_baseline(args.baseline)
        check_phase2_evidence.verify_provenance(baseline)
        if args.capture_profile:
            value = capture_profile(args.capture_profile, baseline)
            target = receipt_path("profile", args.capture_profile)
            write_receipt(target, value)
            print(f"[OK] captured {args.capture_profile} profile: {target}")
            return 0
        if args.capture_diagnostic:
            expected = expected_diagnostic_compositions()
            value = capture_diagnostic(args.capture_diagnostic, expected)
            target = receipt_path(
                "diagnostic", CAPABILITY_SLUGS[args.capture_diagnostic]
            )
            write_receipt(target, value)
            print(f"[OK] captured diagnostic {args.capture_diagnostic}: {target}")
            return 0
        if args.capture_conformance:
            value = capture_conformance()
            target = receipt_path("conformance")
            write_receipt(target, value)
            print(f"[OK] captured Cube compile-conformance: {target}")
            return 0
        current = validate_result_document(build_result(baseline))
        if args.write_result:
            args.write_result.parent.mkdir(parents=True, exist_ok=True)
            args.write_result.write_bytes(canonical_json_bytes(current))
            print(f"[OK] wrote Phase 2 rolling result: {args.write_result}")
            return 0
        assert args.check_result
        ensure_tracked_result(args.check_result)
        tracked = validate_result_document(
            read_canonical_json(args.check_result, "tracked Phase 2 result")
        )
        if tracked != current:
            raise RuntimeError("tracked Phase 2 rolling result is stale")
        print("[OK] Phase 2 resources/build matrix matches the rolling result")
        return 0
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as exc:
        print(f"[ERR] Phase 2 resource qualification failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
