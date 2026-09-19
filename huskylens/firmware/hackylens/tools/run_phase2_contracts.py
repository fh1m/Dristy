#!/usr/bin/env python3
"""Run one provider-independent Phase 2 suite against fake and K210."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from typing import Any, Iterable, Mapping


ROOT = Path(__file__).resolve().parents[1]
TEST_ROOT = ROOT / "tests"

NORMATIVE_SUITES: tuple[dict[str, Any], ...] = (
    {
        "capability": "time",
        "suite_source": "tests/time_capability_harness.c",
        "case_ids": (
            "now-deadline-sliced-sleep", "already-reached",
            "deadline-required", "cancel-before-sleep",
            "cancel-after-slices", "cancel-deadline-boundary",
            "target-wins-at-boundary", "frozen-clock-quarantine",
            "monotonic-regression", "duration-limits-overflow",
        ),
        "backends": {
            "fake": {
                "test": "test_time_capability.TimeCapabilityTests."
                        "test_fake_passes_time_normative_contract_and_bounded_object",
            },
            "k210": {
                "test": "test_time_capability.TimeCapabilityTests."
                        "test_k210_passes_same_time_normative_contract",
            },
        },
    },
    {
        "capability": "input",
        "suite_source": "tests/input_capability_harness.c",
        "case_ids": (
            "inventory", "bounce-debounce", "held-no-repeat",
            "independent-cursors", "multi-button-edge", "bounded-overflow",
            "wrong-context-deadline", "owner-cleanup-stale-handle",
        ),
        "backends": {
            "fake": {
                "test": "test_input_capability.InputCapabilityTests."
                        "test_fake_passes_input_normative_contract_and_bounded_object",
            },
            "k210": {
                "test": "test_input_capability.InputCapabilityTests."
                        "test_k210_passes_same_input_normative_contract",
            },
        },
    },
    {
        "capability": "display",
        "suite_source": "tests/display_normative_suite.c",
        "case_ids": (
            "surface-mutate-abort", "retained-batch-abort",
            "partial-failure-retry", "partial-failure-abort-release",
            "base-overlay-coexist", "disjoint-surface-repair",
            "failed-cleanup-quarantine",
        ),
        "backends": {
            "fake": {
                "test": "test_display_contract.DisplayContractTests."
                        "test_fake_proves_display_state_machine",
            },
            "k210": {
                "test": "test_micropython_bindings."
                        "MicroPythonBindingSafetyTests."
                        "test_k210_display_adapter_composition_cancel_and_cleanup",
            },
        },
    },
    {
        "capability": "external-link",
        "suite_source": "tests/external_link_normative_suite.c",
        "case_ids": (
            "exclusive-acquire-and-info", "malformed-abi",
            "uart-bounded-progress-cancel", "i2c-controller-progress",
            "i2c-target-preload", "failed-cleanup-quarantine",
        ),
        "backends": {
            "fake": {
                "test": "test_external_link_contract."
                        "ExternalLinkContractTests."
                        "test_fake_proves_external_link_state_machine",
            },
            "k210": {
                "test": "test_external_link_contract."
                        "ExternalLinkContractTests."
                        "test_k210_adapter_passes_the_same_normative_suite",
            },
        },
    },
    {
        "capability": "lights",
        "suite_source": "tests/lights_capability_harness.c",
        "case_ids": (
            "declared-features", "exclusive-channel-acquire", "inventory",
            "wrong-owner", "channel-scope", "level-limit", "cancel",
            "deadline", "successful-writes", "failed-release-retains-lease",
            "safe-release", "same-owner-disjoint-lease", "owner-cleanup",
            "stale-handle-replacement", "malformed-request",
            "failed-cleanup-quarantine",
        ),
        "backends": {
            "fake": {
                "test": "test_lights_capability.LightsCapabilityTests."
                        "test_fake_passes_lights_normative_contract_and_bounded_object",
            },
            "k210": {
                "test": "test_lights_capability.LightsCapabilityTests."
                        "test_k210_passes_same_lights_normative_contract",
            },
        },
    },
)

# Provider-specific implementation checks supplement, but can never substitute
# for, the shared suite above.
ADAPTER_SPECIFIC_TESTS: tuple[str, ...] = (
    "test_time_capability.TimeCapabilityTests."
    "test_k210_any_core_clock_read_is_inside_provider_lock",
    "test_input_capability.InputCapabilityTests."
    "test_k210_raw_sampler_is_gated_to_ten_milliseconds",
    "test_lights_capability.LightsCapabilityTests."
    "test_k210_adapter_converts_only_after_cancel_and_deadline_checks",
)

SUPPLEMENTAL_TESTS: tuple[str, ...] = (
    "test_capability_core.CapabilityCoreHostTests."
    "test_common_lifecycle_contract_suite",
    "test_phase2_qualification.Phase2QualificationTests."
    "test_registry_validation_host_p99",
    "test_pong.PongHostTests.test_fixed_step_physics_and_dirty_rendering",
)


def canonical_json_bytes(value: Any) -> bytes:
    return (json.dumps(
        value, ensure_ascii=False, allow_nan=False, indent=2, sort_keys=True,
        separators=(",", ": "),
    ) + "\n").encode("utf-8")


def normalized_text_sha256(path: Path) -> str:
    source = path.read_bytes()
    if b"\x00" in source:
        raise RuntimeError(f"normative suite source is not text: {path}")
    normalized = source.replace(b"\r\n", b"\n").replace(b"\r", b"\n")
    return hashlib.sha256(normalized).hexdigest()


def validate_normative_suites(suites: Iterable[Mapping[str, Any]]) -> None:
    expected = {"time", "input", "display", "external-link", "lights"}
    seen: set[str] = set()
    for suite in suites:
        if set(suite) != {
            "capability", "suite_source", "case_ids", "backends",
        }:
            raise RuntimeError("normative suite has missing or unknown fields")
        capability = suite["capability"]
        if not isinstance(capability, str) or capability in seen:
            raise RuntimeError("normative capability is invalid or duplicated")
        seen.add(capability)
        source = suite["suite_source"]
        if not isinstance(source, str) or not source.startswith("tests/"):
            raise RuntimeError("normative suite source is invalid")
        if not (ROOT / source).is_file():
            raise RuntimeError(f"normative suite source is missing: {source}")
        cases = suite["case_ids"]
        if not isinstance(cases, (tuple, list)) or not cases or any(
            not isinstance(case, str) or not case for case in cases
        ) or len(set(cases)) != len(cases):
            raise RuntimeError("normative case manifest is invalid")
        backends = suite["backends"]
        if not isinstance(backends, Mapping) or list(backends) != [
            "fake", "k210",
        ]:
            raise RuntimeError(
                "normative suite must have ordered fake and K210 backends"
            )
        for backend, binding in backends.items():
            if not isinstance(binding, Mapping) or set(binding) != {"test"}:
                raise RuntimeError(
                    f"{capability}/{backend} attempts a backend-local suite "
                    "or case-manifest substitution"
                )
            selector = binding["test"]
            if not isinstance(selector, str) or not selector:
                raise RuntimeError("normative backend test selector is invalid")
    if seen != expected:
        raise RuntimeError("normative capability set is not exactly Phase 2")


def normative_manifest() -> list[dict[str, Any]]:
    validate_normative_suites(NORMATIVE_SUITES)
    result = []
    for suite in NORMATIVE_SUITES:
        source = suite["suite_source"]
        source_sha = normalized_text_sha256(ROOT / source)
        result.append({
            "capability": suite["capability"],
            "suite_source": source,
            "suite_source_sha256": source_sha,
            "case_ids": list(suite["case_ids"]),
            "backends": {
                name: {"test": binding["test"]}
                for name, binding in suite["backends"].items()
            },
        })
    return result


def matrix_document(*, passed: bool) -> dict[str, Any]:
    manifest = {
        "schema": 2,
        "suite": "phase2-shared-provider-contracts-v2",
        "suite_source_normalization": "lf-text-sha256",
        "normative_suites": normative_manifest(),
        "adapter_specific_tests": list(ADAPTER_SPECIFIC_TESTS),
        "supplemental_tests": list(SUPPLEMENTAL_TESTS),
    }
    return {
        **manifest,
        "manifest_sha256": hashlib.sha256(
            canonical_json_bytes(manifest)
        ).hexdigest(),
        "passed": passed,
    }


def run_suite() -> None:
    validate_normative_suites(NORMATIVE_SUITES)
    selectors = [
        binding["test"]
        for suite in NORMATIVE_SUITES
        for binding in suite["backends"].values()
    ]
    selectors.extend(ADAPTER_SPECIFIC_TESTS)
    selectors.extend(SUPPLEMENTAL_TESTS)
    subprocess.run(
        [sys.executable, "-m", "unittest", "-v", *selectors],
        cwd=TEST_ROOT,
        check=True,
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write-receipt", type=Path)
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args(argv)
    if args.list:
        print(canonical_json_bytes(matrix_document(passed=False)).decode(), end="")
        return 0
    try:
        run_suite()
    except (RuntimeError, subprocess.CalledProcessError) as exc:
        return_code = getattr(exc, "returncode", 1) or 1
        print(
            f"[ERR] Phase 2 contract suite failed: {exc}",
            file=sys.stderr,
        )
        return return_code
    document = matrix_document(passed=True)
    if args.write_receipt:
        args.write_receipt.parent.mkdir(parents=True, exist_ok=True)
        args.write_receipt.write_bytes(canonical_json_bytes(document))
    print(
        "[OK] Phase 2 shared provider suite passed: 5 capability suites "
        "x identical fake/K210 cases plus adapter, lifecycle, p99 and Pong gates"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
