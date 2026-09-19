import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class DisplayContractTests(unittest.TestCase):
    @staticmethod
    def compiler() -> str:
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            raise unittest.SkipTest("host C compiler not installed")
        return compiler

    def test_fake_proves_display_state_machine(self) -> None:
        compiler = self.compiler()
        with tempfile.TemporaryDirectory(prefix="hackylens-display-") as temp:
            executable = Path(temp) / (
                "display_contract.exe" if os.name == "nt" else "display_contract"
            )
            subprocess.run([
                compiler,
                "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                f"-I{ROOT / 'firmware' / 'include'}",
                f"-I{ROOT / 'tests'}",
                str(ROOT / "tests" / "display_contract_harness.c"),
                str(ROOT / "tests" / "display_normative_suite.c"),
                str(ROOT / "tests" / "capability_fake_display.c"),
                "-o", str(executable),
            ], check=True, cwd=ROOT)
            result = subprocess.run(
                [str(executable)], check=True, cwd=ROOT,
                text=True, capture_output=True, timeout=30,
            )
        self.assertIn(
            "DISPLAY_CONTRACT_OK cases=15 normative=7 full_bytes=384 slice_bytes=8",
            result.stdout,
        )

    def test_public_abi_and_fake_are_fixed_capacity(self) -> None:
        compiler = self.compiler()
        source = """
            #include <hackylens/capability/display.h>
            #include <stddef.h>
            _Static_assert(sizeof(hk_display_t) == sizeof(hk_lease_t),
                           "display handle must remain one lease token");
            _Static_assert(HK_CAPABILITY_ID_DISPLAY == 0x00010003U,
                           "display capability ID changed");
            _Static_assert(HK_DISPLAY_FORMAT_RGB565_BE == 1U,
                           "RGB565 byte format changed");
            int main(void) {
                hk_capability_request_t request = HK_DISPLAY_REQUEST_0_1_INIT;
                return request.id == HK_CAPABILITY_ID_DISPLAY ? 0 : 1;
            }
        """
        with tempfile.TemporaryDirectory(prefix="hackylens-display-abi-") as temp:
            temporary = Path(temp)
            translation_unit = temporary / "abi.c"
            executable = temporary / (
                "abi.exe" if os.name == "nt" else "abi"
            )
            fake_object = temporary / "fake.o"
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
                str(ROOT / "tests" / "capability_fake_display.c"),
                "-o", str(fake_object),
            ], check=True, cwd=ROOT)
            nm = shutil.which("nm")
            if nm:
                symbols = subprocess.run(
                    [nm, "-u", str(fake_object)], check=True,
                    text=True, capture_output=True,
                ).stdout.lower()
                for forbidden in (
                    "malloc", "calloc", "realloc", "free", "task", "queue"
                ):
                    self.assertNotIn(forbidden, symbols)

    def test_normative_contract_closes_phase_2_7_ambiguities(self) -> None:
        contract = (
            ROOT / "docs" / "spec" / "capabilities" / "DISPLAY.md"
        ).read_text(encoding="utf-8")
        for required in (
            "RGB565 big-endian",
            "zero-area",
            "successful present, abort, or release",
            "MUST NOT mutate staged state",
            "needs_repair",
            "HK_ERR_LIMIT",
            "HK_ERR_DEADLINE_EXCEEDED",
            "MUST NOT silently promote",
            "source_x = clipped.x - destination.x",
            "MUST NOT replace disjoint damage with one bounding rectangle",
            "does not roll back backing pixel mutations",
            "current backing store",
            "Retained command batches",
            "Phase 2.13",
        ):
            self.assertIn(required, contract)

    def test_phase_2_8_enters_production_through_the_capability(self) -> None:
        for manifest in (
            ROOT / "firmware" / "app_requirements.toml",
            ROOT / "firmware" / "capability_consumers.toml",
        ):
            self.assertIn(
                "hackylens.cap.display",
                manifest.read_text(encoding="utf-8"),
            )
        self.assertTrue(
            (ROOT / "platforms" / "k210" / "capabilities" /
             "display_adapter.c").exists()
        )
        catalog = (
            ROOT / "platforms" / "k210" / "capabilities.toml"
        ).read_text(encoding="utf-8")
        self.assertIn('id = "hackylens.cap.display"', catalog)
        self.assertIn(
            'provider_source = "platforms/k210/capabilities/display_adapter.c"',
            catalog,
        )
        production = "\n".join(
            path.read_text(encoding="utf-8")
            for path in sorted((ROOT / "firmware" / "src").rglob("*"))
            if path.suffix in {".c", ".h"}
        )
        self.assertIn("hackylens/capability/display.h", production)
        self.assertNotIn("hk_lcd.h", production)
        self.assertNotIn("lcd_overlay_", production)
        self.assertTrue((ROOT / "firmware" / "src" / "drivers" /
                         "lcd_st7789.c").is_file())
        self.assertTrue((ROOT / "firmware" / "src" / "drivers" /
                         "lcd_st7789_transport.h").is_file())
        transport = (ROOT / "firmware" / "src" / "drivers" /
                     "lcd_st7789.c").read_text(encoding="utf-8")
        adapter = (ROOT / "platforms" / "k210" / "capabilities" /
                   "display_adapter.c").read_text(encoding="utf-8")
        self.assertEqual(transport.count("LCD_W * LCD_H * 2U"), 1)
        self.assertNotIn("LCD_W * LCD_H * 2U", adapter)
        self.assertNotIn("HK_ENABLE_APP_MICROPYTHON", transport)


if __name__ == "__main__":
    unittest.main()
