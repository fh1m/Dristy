from __future__ import annotations

import os
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"


def load_tool(name: str):
    spec = importlib.util.spec_from_file_location(name, TOOLS / f"{name}.py")
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


hardware = load_tool("check_phase2_hardware")


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def timing(*, maximum: int = 3000) -> dict[str, int]:
    return {"max": maximum, "min": 100, "p99": maximum - 100, "samples": 20}


class Phase2HardwareEvidenceTests(unittest.TestCase):
    def make_impact_evidence(self, root: Path) -> dict[str, object]:
        (root / "VERSION").write_text("0.4.0\n", encoding="utf-8")
        observations = [
            {
                "id": identifier,
                "method": "operator physical check",
                "result": "passed on connected SEN0305",
                "status": "pass",
            }
            for identifier in sorted(hardware.IMPACT_RESULT_IDS)
        ]
        return {
            "accepted": True,
            "board": {
                "id": "sen0305",
                "revision": "SEN0305",
                "serial": "owner-device",
            },
            "current_image": {
                "boot_sanity": True,
                "image_bytes": 1_543_928,
                "image_sha256": "1" * 64,
                "source_manifest_sha256": "2" * 64,
                "version": "0.4.0",
            },
            "excluded_boards": ["sipeed-maix-cube"],
            "impact_review": {
                "broader_retest_required": False,
                "carried_forward": ["time", "uart", "i2c"],
                "changed_areas": ["buttons", "files", "micropython-ui"],
                "provider_hal_routing_changed": False,
                "retested_areas": ["buttons", "files", "micropython-ui"],
            },
            "limitations": [
                "Historical timing samples were not reconstructed or invented."
            ],
            "observations": observations,
            "operator": {"identity": "project-owner"},
            "phase": "2.13",
            "policy": "impact-based-owner-acceptance",
            "recorded_at": "2026-08-24T15:00:00Z",
            "schema": 2,
        }

    def make_evidence(self, root: Path) -> dict[str, object]:
        evidence_dir = root / "docs" / "evidence"
        artifact_dir = evidence_dir / "phase2-hardware"
        artifact_dir.mkdir(parents=True, exist_ok=True)
        (root / "VERSION").write_text(
            (ROOT / "VERSION").read_text(encoding="utf-8"),
            encoding="utf-8",
            newline="\n",
        )
        baseline_path = evidence_dir / "phase2-baseline.json"
        baseline_path.write_bytes(
            (ROOT / "docs" / "evidence" / "phase2-baseline.json").read_bytes()
        )
        baseline = json.loads(baseline_path.read_text(encoding="utf-8"))

        candidate_path = evidence_dir / "phase2-candidate-result.json"
        candidate_path.write_bytes(
            (ROOT / "docs" / "evidence" / "phase2-result.json").read_bytes()
        )
        candidate = json.loads(candidate_path.read_text(encoding="utf-8"))
        full = candidate["builds"]["profiles"]["full"]

        fixture_path = artifact_dir / "fixture.md"
        raw_path = artifact_dir / "raw.log"
        timing_path = artifact_dir / "timings.csv"
        fixture_path.write_text("fixture SEN0305-EXT-01\n", encoding="utf-8")
        raw_path.write_text("physical smoke raw log\n", encoding="utf-8")
        timing_path.write_text("metric,min,p99,max,samples\n", encoding="utf-8")
        fixture_relative = "docs/evidence/phase2-hardware/fixture.md"
        raw_relative = "docs/evidence/phase2-hardware/raw.log"
        timing_relative = "docs/evidence/phase2-hardware/timings.csv"

        version = (root / "VERSION").read_text(encoding="utf-8").strip()
        common_artifacts = [raw_relative, timing_relative]
        return {
            "accepted": True,
            "artifacts": [
                {
                    "kind": "fixture-schematic",
                    "path": fixture_relative,
                    "sha256": digest(fixture_path),
                },
                {
                    "kind": "raw-log",
                    "path": raw_relative,
                    "sha256": digest(raw_path),
                },
                {
                    "kind": "timing-log",
                    "path": timing_relative,
                    "sha256": digest(timing_path),
                },
            ],
            "board": {
                "id": "sen0305",
                "revision": "SEN0305-rev-a",
                "serial": "test-board-001",
            },
            "candidate": {
                "firmware": {
                    "attestation_sha256": full["attestation_sha256"],
                    "capabilities_sha256": full["capabilities_sha256"],
                    "composition_sha256": full["composition_sha256"],
                    "image_bytes": full["image"]["raw_bytes"],
                    "image_sha256": full["image"]["sha256"],
                    "version": version,
                },
                "result": {
                    "path": "docs/evidence/phase2-candidate-result.json",
                    "sha256": digest(candidate_path),
                },
                "source_commit": "1" * 40,
                "source_manifest_sha256": candidate["source"]["manifest_sha256"],
                "toolchain": {
                    "archive_sha256": baseline["toolchain"]["archive_sha256"],
                    "host_cmake": "4.2.3",
                    "host_python": "3.14.3",
                    "kendryte_standalone_sdk_revision": baseline["toolchain"][
                        "kendryte_standalone_sdk_revision"
                    ],
                    "kendryte_toolchain": baseline["toolchain"]["kendryte_toolchain"],
                },
                "workflow": {
                    "run_id": 123456,
                    "url": "https://github.com/aztechell/dristy/actions/runs/123456",
                },
            },
            "excluded_boards": ["sipeed-maix-cube"],
            "fixture": {
                "i2c": {
                    "connector": "external-four-pin",
                    "scl_io": 34,
                    "sda_io": 35,
                    "target_address_7bit": 0x50,
                    "target_identity": "deterministic-test-target-v1",
                },
                "id": "SEN0305-EXT-01",
                "schematic": {
                    "path": fixture_relative,
                    "sha256": digest(fixture_path),
                },
                "uart": {
                    "connector": "external-four-pin",
                    "loopback": True,
                    "rx_io": 34,
                    "tx_io": 35,
                },
            },
            "operator": {"identity": "test-operator"},
            "phase": "2.13",
            "recorded_at": "2026-08-22T12:00:00Z",
            "results": {
                "buttons": {
                    "artifact_paths": common_artifacts,
                    "debounce_latency_us": timing(),
                    "event_latency_us": timing(),
                    "logical_buttons": ["left", "ok", "right", "back"],
                    "press_release_hold_passed": True,
                    "repeated_edges_during_hold": 0,
                    "status": "pass",
                },
                "display": {
                    "artifact_paths": common_artifacts,
                    "coverage": list(hardware.DISPLAY_COVERAGE),
                    "full_present_us": timing(maximum=400_000),
                    "matched_workload_regression_percent": 7.5,
                    "status": "pass",
                },
                "external_service_restore": {
                    "artifact_paths": [raw_relative],
                    "configured_transport": "uart",
                    "hmpy_round_trips": 3,
                    "status": "pass",
                },
                "i2c": {
                    "artifact_paths": [raw_relative],
                    "controller_read_passed": True,
                    "controller_write_passed": True,
                    "error_recovery_passed": True,
                    "mode_switch_passed": True,
                    "status": "pass",
                    "target_address_7bit": 0x50,
                },
                "lights": {
                    "artifact_paths": [raw_relative],
                    "cleanup_safe_off": True,
                    "coverage": list(hardware.LIGHTS_COVERAGE),
                    "persisted_restore": True,
                    "status": "pass",
                },
                "regression": {
                    "artifact_paths": [raw_relative],
                    "coverage": list(hardware.REGRESSION_COVERAGE),
                    "status": "pass",
                },
                "time": {
                    "artifact_paths": [timing_relative],
                    "cancel_latency_us": timing(),
                    "read_overhead_us": timing(),
                    "sleep_latency_us": timing(),
                    "status": "pass",
                },
                "uart": {
                    "artifact_paths": [raw_relative],
                    "baud": 115200,
                    "late_bytes_after_cancel": 0,
                    "loopback_rx_bytes": 256,
                    "payload_bytes": 256,
                    "status": "pass",
                    "wire_drain_observed": True,
                },
            },
            "schema": 1,
        }

    def with_fixture(self):
        return tempfile.TemporaryDirectory(prefix="dristy-phase2-hardware-")

    def test_complete_exact_image_evidence_passes(self) -> None:
        with self.with_fixture() as temp:
            root = Path(temp)
            evidence = self.make_evidence(root)
            self.assertIs(hardware.validate_document(evidence, root), evidence)

    def test_impact_based_owner_evidence_passes_without_fabricated_timings(self) -> None:
        with self.with_fixture() as temp:
            root = Path(temp)
            evidence = self.make_impact_evidence(root)
            self.assertIs(hardware.validate_document(evidence, root), evidence)

            evidence["observations"].pop()
            with self.assertRaisesRegex(
                hardware.HardwareEvidenceError, "coverage is incomplete"
            ):
                hardware.validate_document(evidence, root)

    def test_impact_review_requires_every_changed_area_to_be_retested(self) -> None:
        with self.with_fixture() as temp:
            root = Path(temp)
            evidence = self.make_impact_evidence(root)
            evidence["impact_review"]["retested_areas"].remove("files")
            with self.assertRaisesRegex(
                hardware.HardwareEvidenceError, "must be retested"
            ):
                hardware.validate_document(evidence, root)

    def test_candidate_image_or_source_mismatch_is_rejected(self) -> None:
        with self.with_fixture() as temp:
            root = Path(temp)
            evidence = self.make_evidence(root)
            evidence["candidate"]["firmware"]["image_sha256"] = "0" * 64
            with self.assertRaisesRegex(
                hardware.HardwareEvidenceError, "does not match the full profile"
            ):
                hardware.validate_document(evidence, root)

            evidence = self.make_evidence(root)
            evidence["candidate"]["source_manifest_sha256"] = "0" * 64
            with self.assertRaisesRegex(
                hardware.HardwareEvidenceError, "source manifest mismatch"
            ):
                hardware.validate_document(evidence, root)

    def test_mutable_rolling_result_name_is_rejected(self) -> None:
        with self.with_fixture() as temp:
            root = Path(temp)
            evidence = self.make_evidence(root)
            source = root / "docs" / "evidence" / "phase2-candidate-result.json"
            rolling = source.with_name("phase2-result.json")
            rolling.write_bytes(source.read_bytes())
            evidence["candidate"]["result"] = {
                "path": "docs/evidence/phase2-result.json",
                "sha256": digest(rolling),
            }
            with self.assertRaisesRegex(
                hardware.HardwareEvidenceError, "immutable phase2-candidate-result"
            ):
                hardware.validate_document(evidence, root)

    def test_missing_fixture_or_non_pass_result_is_rejected(self) -> None:
        with self.with_fixture() as temp:
            root = Path(temp)
            evidence = self.make_evidence(root)
            evidence["fixture"]["uart"]["loopback"] = False
            with self.assertRaisesRegex(
                hardware.HardwareEvidenceError, "loopback is not canonical"
            ):
                hardware.validate_document(evidence, root)

            evidence = self.make_evidence(root)
            evidence["results"]["i2c"]["status"] = "not-run"
            with self.assertRaisesRegex(hardware.HardwareEvidenceError, "must be pass"):
                hardware.validate_document(evidence, root)

    def test_display_physical_bounds_are_enforced(self) -> None:
        with self.with_fixture() as temp:
            root = Path(temp)
            evidence = self.make_evidence(root)
            evidence["results"]["display"]["full_present_us"] = timing(
                maximum=500_001
            )
            with self.assertRaisesRegex(hardware.HardwareEvidenceError, "exceeded 500 ms"):
                hardware.validate_document(evidence, root)

            evidence = self.make_evidence(root)
            evidence["results"]["display"][
                "matched_workload_regression_percent"
            ] = 10.01
            with self.assertRaisesRegex(hardware.HardwareEvidenceError, "exceeded 10%"):
                hardware.validate_document(evidence, root)

    def test_artifact_tampering_is_rejected(self) -> None:
        with self.with_fixture() as temp:
            root = Path(temp)
            evidence = self.make_evidence(root)
            raw = root / "docs" / "evidence" / "phase2-hardware" / "raw.log"
            raw.write_text("tampered\n", encoding="utf-8")
            with self.assertRaisesRegex(hardware.HardwareEvidenceError, "hash mismatch"):
                hardware.validate_document(evidence, root)

    def test_version_change_invalidates_hardware_candidate(self) -> None:
        with self.with_fixture() as temp:
            root = Path(temp)
            evidence = self.make_evidence(root)
            (root / "VERSION").write_text("9.9.9\n", encoding="utf-8")
            with self.assertRaisesRegex(
                hardware.HardwareEvidenceError, "differs from the repository VERSION"
            ):
                hardware.validate_document(evidence, root)

    def test_release_workflow_validates_evidence_when_present(self) -> None:
        workflow = (ROOT / ".github" / "workflows" / "release.yml").read_text(
            encoding="utf-8"
        )
        existence_check = (
            "Test-Path -LiteralPath "
            "docs/evidence/phase2-hardware-smoke.json"
        )
        validator = "Invoke-NativeChecked python tools/check_phase2_hardware.py"
        self.assertIn(existence_check, workflow)
        self.assertIn(validator, workflow)
        self.assertLess(workflow.index(existence_check), workflow.index(validator))


@unittest.skipUnless(os.path.exists("/dev/ttyUSB0"), "no /dev/ttyUSB0 attached")
class LiveDristyHardwareSmoke(unittest.TestCase):
    def test_serial_port_readable_or_sudo_documented(self) -> None:
        try:
            with open("/dev/ttyUSB0", "rb", buffering=0):
                pass
        except PermissionError:
            self.skipTest("add user to dialout or use sudo for live smoke")


if __name__ == "__main__":
    unittest.main()
