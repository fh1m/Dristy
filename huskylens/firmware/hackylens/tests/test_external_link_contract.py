import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ExternalLinkContractTests(unittest.TestCase):
    @staticmethod
    def compiler() -> str:
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            raise unittest.SkipTest("host C compiler not installed")
        return compiler

    def test_fake_proves_external_link_state_machine(self) -> None:
        compiler = self.compiler()
        with tempfile.TemporaryDirectory(prefix="hackylens-external-link-") as temp:
            executable = Path(temp) / (
                "external_link_contract.exe" if os.name == "nt"
                else "external_link_contract"
            )
            subprocess.run([
                compiler,
                "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                f"-I{ROOT / 'firmware' / 'include'}",
                f"-I{ROOT / 'tests'}",
                str(ROOT / "tests" / "external_link_contract_harness.c"),
                str(ROOT / "tests" / "external_link_normative_suite.c"),
                str(ROOT / "tests" / "capability_fake_external_link.c"),
                "-o", str(executable),
            ], check=True, cwd=ROOT)
            result = subprocess.run(
                [str(executable)], check=True, cwd=ROOT,
                text=True, capture_output=True, timeout=30,
            )
        self.assertIn(
            "EXTERNAL_LINK_CONTRACT_OK cases=6 normative=1 burst=32 "
            "fixed_capacity=256",
            result.stdout,
        )

    def test_k210_adapter_passes_the_same_normative_suite(self) -> None:
        compiler = self.compiler()
        with tempfile.TemporaryDirectory(prefix="hackylens-k210-external-") as temp:
            executable = Path(temp) / (
                "k210_external_link.exe" if os.name == "nt"
                else "k210_external_link"
            )
            subprocess.run([
                compiler,
                "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                f"-I{ROOT / 'firmware' / 'include'}",
                f"-I{ROOT / 'firmware' / 'src' / 'capabilities'}",
                f"-I{ROOT / 'platforms' / 'k210' / 'hal'}",
                f"-I{ROOT / 'tests'}",
                str(ROOT / "tests" / "k210_external_link_harness.c"),
                str(ROOT / "tests" / "external_link_normative_suite.c"),
                str(ROOT / "firmware" / "src" / "capabilities" /
                    "external_link.c"),
                str(ROOT / "platforms" / "k210" / "capabilities" /
                    "external_link_adapter.c"),
                "-o", str(executable),
            ], check=True, cwd=ROOT)
            result = subprocess.run(
                [str(executable)], check=True, cwd=ROOT,
                text=True, capture_output=True, timeout=30,
            )
        self.assertIn(
            "K210_EXTERNAL_LINK_OK normative=1 target_handoff=1 "
            "uart_loopback=1 "
            "uart=102 i2c_tx=20",
            result.stdout,
        )

    def test_public_abi_and_fake_are_fixed_capacity(self) -> None:
        compiler = self.compiler()
        source = """
            #include <hackylens/capability/external_link.h>
            _Static_assert(sizeof(hk_external_link_t) == sizeof(hk_lease_t),
                           "external-link handle must be one lease token");
            _Static_assert(sizeof(hk_external_link_op_t) == 8U,
                           "operation token must remain fixed-shape");
            _Static_assert(HK_CAPABILITY_ID_EXTERNAL_LINK == 0x00010004U,
                           "external-link capability ID changed");
            _Static_assert(HK_EXTERNAL_LINK_FEATURES_0_1 == 7U,
                           "external-link 0.1 feature bits changed");
            _Static_assert(HK_EXTERNAL_LINK_TARGET_FILL_BYTE == 0U,
                           "target fill byte changed");
            int main(void) {
                hk_capability_request_t request =
                    HK_EXTERNAL_LINK_REQUEST_0_1_INIT;
                return request.id == HK_CAPABILITY_ID_EXTERNAL_LINK ? 0 : 1;
            }
        """
        with tempfile.TemporaryDirectory(
            prefix="hackylens-external-link-abi-"
        ) as temp:
            temporary = Path(temp)
            translation_unit = temporary / "abi.c"
            executable = temporary / ("abi.exe" if os.name == "nt" else "abi")
            fake_object = temporary / "fake.o"
            adapter_object = temporary / "k210_adapter.o"
            translation_unit.write_text(source, encoding="utf-8")
            common = [
                "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                f"-I{ROOT / 'firmware' / 'include'}",
                f"-I{ROOT / 'tests'}",
            ]
            subprocess.run([
                compiler, *common, str(translation_unit), "-o", str(executable),
            ], check=True, cwd=ROOT)
            subprocess.run([str(executable)], check=True, cwd=ROOT)
            subprocess.run([
                compiler, *common, "-c",
                str(ROOT / "tests" / "capability_fake_external_link.c"),
                "-o", str(fake_object),
            ], check=True, cwd=ROOT)
            subprocess.run([
                compiler, *common,
                f"-I{ROOT / 'firmware' / 'src' / 'capabilities'}",
                f"-I{ROOT / 'platforms' / 'k210' / 'hal'}",
                "-c",
                str(ROOT / "platforms" / "k210" / "capabilities" /
                    "external_link_adapter.c"),
                "-o", str(adapter_object),
            ], check=True, cwd=ROOT)
            nm = shutil.which("nm")
            if nm:
                for inspected_object in (fake_object, adapter_object):
                    symbols = subprocess.run(
                        [nm, "-u", str(inspected_object)], check=True,
                        text=True, capture_output=True,
                    ).stdout.lower()
                    for forbidden in (
                        "malloc", "calloc", "realloc", "free", "task", "queue"
                    ):
                        self.assertNotIn(forbidden, symbols)

    def test_normative_contract_closes_phase_2_9_ambiguities(self) -> None:
        contract = (
            ROOT / "docs" / "spec" / "capabilities" / "EXTERNAL_LINK.md"
        ).read_text(encoding="utf-8")
        normalized = " ".join(contract.split())
        for required in (
            "mode_features",
            "one exclusive connector lease",
            "one in-flight operation per lease",
            "at most 32 bytes",
            "empty FIFO and an idle shift register",
            "original absolute deadline",
            "NACK maps to `HK_ERR_IO`",
            "cancellation wins over deadline expiry",
            "completed RX prefix",
            "MUST NOT perform late writes",
            "generation-checked",
            "all-zero typed handle is idempotent `HK_OK`",
            "uses a preload model",
            "one-shot response for the next master READ",
            "atomically replaces an unread preload",
            "pads a longer request with `0x00`",
            "READ event is queued after that master transaction completes",
            "undersized input structure",
            "two bounded completed-event slots",
            "`HK_ERR_OVERFLOW`",
            "explicit resynchronization state",
            "Phase 2.10",
        ):
            self.assertIn(required, normalized)

        fake = (ROOT / "tests" / "capability_fake_external_link.h").read_text(
            encoding="utf-8"
        )
        self.assertNotIn("late_effect_attempts", fake)

    def test_public_contract_contains_no_board_routing_identity(self) -> None:
        header = (
            ROOT / "firmware" / "include" / "hackylens" / "capability" /
            "external_link.h"
        ).read_text(encoding="utf-8").lower()
        for forbidden in ("k210", "sen0305", "route_id", "uart_device"):
            self.assertNotIn(forbidden, header)


if __name__ == "__main__":
    unittest.main()
