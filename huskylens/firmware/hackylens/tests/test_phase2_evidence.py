from __future__ import annotations

import copy
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))


def load_checker():
    path = TOOLS / "check_phase2_evidence.py"
    spec = importlib.util.spec_from_file_location("hackylens_phase2_evidence", path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


CHECKER = load_checker()


class Phase2EvidenceTest(unittest.TestCase):
    def test_repository_baseline_is_canonical_and_provenance_checked(self) -> None:
        document = CHECKER.load_baseline()
        CHECKER.verify_provenance(document)
        self.assertEqual(document["baseline"]["commit"], CHECKER.SOURCE_COMMIT)

    def test_profile_contract_is_exact(self) -> None:
        document = CHECKER.load_baseline()
        profiles = document["baseline"]["profiles"]
        self.assertEqual(set(profiles), {"full", "micropython-disabled"})
        self.assertTrue(profiles["full"]["release_qualified"])
        self.assertEqual(
            profiles["micropython-disabled"]["disabled_apps"], ["micropython"]
        )
        self.assertFalse(profiles["micropython-disabled"]["release_qualified"])

    def test_unknown_baseline_field_is_rejected(self) -> None:
        document = copy.deepcopy(CHECKER.load_baseline())
        document["unknown"] = True
        with self.assertRaisesRegex(RuntimeError, "missing or unknown fields"):
            CHECKER.validate_document(document)

    def test_noncanonical_json_is_rejected_before_digest(self) -> None:
        document = CHECKER.load_baseline()
        with tempfile.TemporaryDirectory(prefix="hackylens-phase2-evidence-") as temp:
            path = Path(temp) / "baseline.json"
            path.write_text(json.dumps(document), encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "not canonical"):
                CHECKER.load_baseline(path)

    def test_pinned_digest_rejects_canonical_mutation(self) -> None:
        document = copy.deepcopy(CHECKER.load_baseline())
        document["toolchain"]["operator_recorded_python"] = "0.0.0"
        with tempfile.TemporaryDirectory(prefix="hackylens-phase2-evidence-") as temp:
            path = Path(temp) / "baseline.json"
            path.write_bytes(CHECKER.canonical_json_bytes(document))
            with self.assertRaisesRegex(RuntimeError, "digest mismatch"):
                CHECKER.load_baseline(path)

    def test_profile_static_ram_formula_is_enforced(self) -> None:
        document = copy.deepcopy(CHECKER.load_baseline())
        document["baseline"]["profiles"]["full"]["elf"]["static_ram_bytes"] += 1
        with self.assertRaisesRegex(RuntimeError, "static_ram_bytes formula mismatch"):
            CHECKER.validate_document(document)

    def test_load_can_be_repointed_to_a_matching_digest(self) -> None:
        document = CHECKER.load_baseline()
        with tempfile.TemporaryDirectory(prefix="hackylens-phase2-evidence-") as temp:
            path = Path(temp) / "baseline.json"
            encoded = CHECKER.canonical_json_bytes(document)
            path.write_bytes(encoded)
            digest = CHECKER.hashlib.sha256(encoded).hexdigest()
            with mock.patch.object(CHECKER, "PINNED_BASELINE_SHA256", digest):
                self.assertEqual(CHECKER.load_baseline(path), document)

    def test_profile_budget_accepts_limits_and_rejects_overages(self) -> None:
        document = CHECKER.load_baseline()
        profile = document["baseline"]["profiles"]["full"]
        acceptance = document["acceptance"]
        erase_size = 4096
        baseline_occupied = profile["image"]["flash_occupied_bytes"]
        allowed_occupied = baseline_occupied + acceptance["flash_delta_max_bytes"]
        allowed_raw = allowed_occupied - CHECKER.build_firmware.K210_IMAGE_OVERHEAD
        baseline_static = profile["elf"]["static_ram_bytes"]
        data_bytes = profile["elf"]["data_bytes"]
        allowed_bss = baseline_static + acceptance["static_ram_delta_max_bytes"] - data_bytes

        self.assertEqual(
            CHECKER.verify_profile_budget(
                document,
                "full",
                raw_bytes=allowed_raw,
                data_bytes=data_bytes,
                bss_bytes=allowed_bss,
                erase_size=erase_size,
            ),
            (acceptance["flash_delta_max_bytes"],
             acceptance["static_ram_delta_max_bytes"]),
        )
        with self.assertRaisesRegex(RuntimeError, "flash delta"):
            CHECKER.verify_profile_budget(
                document,
                "full",
                raw_bytes=allowed_raw + erase_size,
                data_bytes=data_bytes,
                bss_bytes=allowed_bss,
                erase_size=erase_size,
            )
        with self.assertRaisesRegex(RuntimeError, "static RAM delta"):
            CHECKER.verify_profile_budget(
                document,
                "full",
                raw_bytes=allowed_raw,
                data_bytes=data_bytes,
                bss_bytes=allowed_bss + 1,
                erase_size=erase_size,
            )


if __name__ == "__main__":
    unittest.main()
