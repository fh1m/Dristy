#!/usr/bin/env python3
"""Enforce the Phase 2.12 machine-checkable pre-hardware exit gate."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import check_phase2_evidence
import check_phase2_hardware
import check_phase2_resources
import run_phase2_contracts


DEFAULT_RESULT = ROOT / "docs" / "evidence" / "phase2-result.json"
DEFAULT_CLOSURE = ROOT / "docs" / "evidence" / "phase2-closure-result.json"
COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")
WORKFLOW_URL_RE = re.compile(
    r"^https://github\.com/aztechell/hackylens/actions/runs/(\d+)$"
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def linked_document(
    value: Any, *, expected_name: str, label: str
) -> tuple[Path, dict[str, Any]]:
    if not isinstance(value, dict) or set(value) != {"path", "sha256"}:
        raise RuntimeError(f"{label} link fields are invalid")
    relative = value["path"]
    digest = value["sha256"]
    if not isinstance(relative, str) or not isinstance(digest, str):
        raise RuntimeError(f"{label} link values are invalid")
    path = (ROOT / relative).resolve()
    try:
        path.relative_to((ROOT / "docs" / "evidence").resolve())
    except ValueError as exc:
        raise RuntimeError(f"{label} must be under docs/evidence") from exc
    if path.name != expected_name or not path.is_file():
        raise RuntimeError(f"{label} path is invalid")
    if sha256(path) != digest:
        raise RuntimeError(f"{label} digest mismatch")
    return path, check_phase2_resources.read_canonical_json(path, label)


def verify_closure(
    closure: dict[str, Any], *, verify_receipts: bool = True
) -> None:
    expected_fields = {
        "accepted", "automated", "capability_api", "firmware_version",
        "hardware", "impact_review", "implementation", "phase", "schema",
    }
    if not isinstance(closure, dict) or set(closure) != expected_fields:
        raise RuntimeError("Phase 2 closure has missing or unknown fields")
    if closure["schema"] != 1 or closure["phase"] != "2.14" or \
            closure["accepted"] is not True:
        raise RuntimeError("Phase 2 closure identity/status mismatch")
    version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    if closure["firmware_version"] != version or version != "0.4.0":
        raise RuntimeError("Phase 2 closure firmware version mismatch")
    if closure["capability_api"] != {
        "stability": "experimental", "version": "0.1.0",
    }:
        raise RuntimeError("Phase 2 closure Capability API mismatch")

    implementation = closure["implementation"]
    if not isinstance(implementation, dict) or set(implementation) != {
        "commit", "workflow",
    }:
        raise RuntimeError("Phase 2 implementation identity is invalid")
    commit = implementation["commit"]
    if not isinstance(commit, str) or not COMMIT_RE.fullmatch(commit):
        raise RuntimeError("Phase 2 implementation commit is invalid")
    ancestor = subprocess.run(
        ["git", "merge-base", "--is-ancestor", commit, "HEAD"], cwd=ROOT
    )
    if ancestor.returncode != 0:
        raise RuntimeError("Phase 2 implementation commit is not an ancestor")
    workflow = implementation["workflow"]
    if not isinstance(workflow, dict) or set(workflow) != {"run_id", "url"}:
        raise RuntimeError("Phase 2 implementation workflow is invalid")
    run_id = workflow["run_id"]
    url = workflow["url"]
    match = WORKFLOW_URL_RE.fullmatch(url) if isinstance(url, str) else None
    if isinstance(run_id, bool) or not isinstance(run_id, int) or run_id < 1 or \
            not match or int(match.group(1)) != run_id:
        raise RuntimeError("Phase 2 implementation workflow URL/run mismatch")

    automated_path, automated = linked_document(
        closure["automated"], expected_name="phase2-candidate-result.json",
        label="Phase 2 automated evidence",
    )
    del automated_path
    verify_result(automated, verify_receipts=verify_receipts)
    hardware_path, hardware = linked_document(
        closure["hardware"], expected_name="phase2-hardware-smoke.json",
        label="Phase 2 hardware evidence",
    )
    check_phase2_hardware.validate_document(hardware, ROOT)
    if hardware["current_image"]["source_manifest_sha256"] != \
            automated["source"]["manifest_sha256"]:
        raise RuntimeError("Phase 2 hardware/automated source identity mismatch")
    del hardware_path

    if closure["impact_review"] != {
        "closure_changes": "documentation-and-evidence-only",
        "runtime_retest_required": False,
    }:
        raise RuntimeError("Phase 2 closure impact review mismatch")


def verify_workflow(workflow: str) -> None:
    required = (
        "python tools/run_phase2_contracts.py --write-receipt build/phase2-qualification/contracts.json",
        "python tools/check_phase2_resources.py --capture-diagnostic",
        "python tools/build_firmware.py conformance --board sipeed-maix-cube",
        "python tools/check_phase2_resources.py --capture-conformance",
        "python tools/check_phase2_resources.py --capture-profile micropython-disabled",
        "python tools/check_phase2_resources.py --capture-profile full",
        "python tools/check_phase2_resources.py --check-result",
        "python tools/check_phase2_exit.py --mode pre-hardware",
    )
    for command in required:
        if command not in workflow:
            raise RuntimeError(f"release workflow omits Phase 2 gate: {command}")
    disabled_build = workflow.index(
        "build_firmware.py full --board huskylens-sen0305 --disable-app micropython"
    )
    disabled_baseline = workflow.index(
        "check_phase2_evidence.py --verify-profile micropython-disabled"
    )
    disabled_capture = workflow.index(
        "check_phase2_resources.py --capture-profile micropython-disabled"
    )
    full_build = workflow.index(
        "build_firmware.py full --board huskylens-sen0305\n"
    )
    full_baseline = workflow.index(
        "check_phase2_evidence.py --verify-profile full"
    )
    full_capture = workflow.index(
        "check_phase2_resources.py --capture-profile full"
    )
    final_resource = workflow.index("check_phase2_resources.py --check-result")
    final_exit = workflow.index("check_phase2_exit.py --mode pre-hardware")
    if not (
        disabled_build < disabled_baseline < disabled_capture
        and full_build < full_baseline < full_capture
        and full_capture < final_resource < final_exit
    ):
        raise RuntimeError("Phase 2 build capture/exit ordering is unsafe")


def verify_result(
    result: dict[str, Any], *, verify_receipts: bool = True
) -> None:
    check_phase2_resources.validate_result_document(result)
    if result["source"] != check_phase2_resources.repository_source_identity():
        raise RuntimeError("Phase 2 result source identity is stale")
    if result["composition"]["capability_ids"] != list(
        check_phase2_resources.EXPECTED_CAPABILITIES
    ):
        raise RuntimeError("Phase 2 result capability set is not exactly five")
    expected_compositions = (
        check_phase2_resources.expected_diagnostic_compositions()
    )
    if result["composition"]["diagnostic_expectations"] != expected_compositions:
        raise RuntimeError("Phase 2 result diagnostic composition is stale")
    expected_contracts = run_phase2_contracts.matrix_document(passed=True)
    if result["contracts"] != expected_contracts:
        raise RuntimeError("Phase 2 result contract matrix is stale")
    profiles = result["builds"].get("profiles")
    if not isinstance(profiles, dict) or set(profiles) != {
        "full", "micropython-disabled",
    }:
        raise RuntimeError("Phase 2 result firmware profile matrix is incomplete")
    diagnostics = result["builds"].get("capability_absent_diagnostics")
    if not isinstance(diagnostics, dict) or set(diagnostics) != set(
        check_phase2_resources.EXPECTED_CAPABILITIES
    ):
        raise RuntimeError("Phase 2 result diagnostic matrix is incomplete")
    cube = result["builds"].get("cube_conformance", {})
    if cube.get("compile_only") is not True or \
            cube.get("runtime_supported") is not False:
        raise RuntimeError("Cube is not represented as compile-conformance-only")
    if result["resources"].get("accepted") is not True:
        raise RuntimeError("Phase 2 result resource gate failed")
    if result["latency"].get("accepted") is not True:
        raise RuntimeError("Phase 2 result latency gate failed")
    elapsed = result["latency"].get("elapsed_hardware_measurements", {})
    if not elapsed or any(
        value != "deferred-to-phase-2.13" for value in elapsed.values()
    ):
        raise RuntimeError("physical latency deferral is missing or overclaimed")
    if verify_receipts:
        baseline = check_phase2_evidence.load_baseline()
        check_phase2_evidence.verify_provenance(baseline)
        current = check_phase2_resources.build_result(baseline)
        if result != current:
            raise RuntimeError("Phase 2 result does not match build receipts")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--mode", required=True, choices=("pre-hardware", "closure")
    )
    parser.add_argument("--result", type=Path, default=DEFAULT_RESULT)
    parser.add_argument("--closure-result", type=Path, default=DEFAULT_CLOSURE)
    parser.add_argument(
        "--without-build-receipts", action="store_true",
        help="Validate tracked source/schema only (tests and diagnostics).",
    )
    args = parser.parse_args(argv)
    try:
        if args.mode == "closure":
            closure = check_phase2_resources.read_canonical_json(
                args.closure_result, "Phase 2 closure result"
            )
            verify_closure(
                closure, verify_receipts=not args.without_build_receipts
            )
        else:
            result = check_phase2_resources.read_canonical_json(
                args.result, "Phase 2 rolling result"
            )
            verify_result(result, verify_receipts=not args.without_build_receipts)
            workflow = (ROOT / ".github/workflows/release.yml").read_text(
                encoding="utf-8"
            )
            verify_workflow(workflow)
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError,
            json.JSONDecodeError) as exc:
        print(f"[ERR] Phase 2 exit gate failed: {exc}", file=sys.stderr)
        return 1
    if args.mode == "closure":
        print("[OK] Phase 2.14 closure gate passed")
    else:
        print(
            "[OK] Phase 2.12 pre-hardware exit passed; physical SEN0305 "
            "acceptance remains Phase 2.13"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
