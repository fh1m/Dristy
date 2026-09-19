import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class MicroPythonBindingSafetyTests(unittest.TestCase):
    @staticmethod
    def compiler() -> str:
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            raise unittest.SkipTest("host C compiler not installed")
        return compiler

    def test_rpc_cancel_uart_progress_and_cleanup_contract(self) -> None:
        compiler = self.compiler()

        with tempfile.TemporaryDirectory(prefix="hackylens-mpy-bindings-") as temp:
            executable = Path(temp) / (
                "micropython_bindings_test.exe"
                if os.name == "nt"
                else "micropython_bindings_test"
            )
            command = [
                compiler,
                "-std=c11",
                "-O1",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-DMICROPYTHON_BINDING_TESTING",
                f"-I{ROOT / 'tests'}",
                f"-I{ROOT / 'firmware' / 'include'}",
                f"-I{ROOT / 'firmware' / 'src' / 'services'}",
                f"-I{ROOT / 'firmware' / 'src' / 'adapters' / 'micropython'}",
                f"-I{ROOT / 'firmware' / 'src' / 'drivers'}",
                f"-I{ROOT / 'firmware' / 'src' / 'capabilities'}",
                f"-I{ROOT / 'firmware' / 'src' / 'config'}",
                f"-I{ROOT / 'firmware' / 'assets'}",
                str(ROOT / "tests" / "micropython_bindings_harness.c"),
                str(
                    ROOT
                    / "firmware"
                    / "src"
                    / "adapters"
                    / "micropython"
                    / "micropython_capability_bridge.c"
                ),
                "-o",
                str(executable),
            ]
            subprocess.run(command, check=True, cwd=ROOT)
            result = subprocess.run(
                [str(executable)],
                check=True,
                cwd=ROOT,
                text=True,
                capture_output=True,
                timeout=30,
            )

        self.assertIn("MICROPYTHON_BINDINGS_OK cases=9", result.stdout)

    def test_uart_fifo_capacity_boundaries(self) -> None:
        compiler = self.compiler()

        with tempfile.TemporaryDirectory(prefix="hackylens-uart-hal-") as temp:
            executable = Path(temp) / (
                "hal_external_link_test.exe"
                if os.name == "nt"
                else "hal_external_link_test"
            )
            command = [
                compiler,
                "-std=c11",
                "-O1",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-DHAL_EXTERNAL_LINK_TESTING",
                f"-I{ROOT / 'tests'}",
                f"-I{ROOT / 'platforms' / 'k210' / 'hal'}",
                str(ROOT / "tests" / "hal_external_link_harness.c"),
                str(
                    ROOT
                    / "platforms"
                    / "k210"
                    / "hal"
                    / "hal_external_link.c"
                ),
                "-o",
                str(executable),
            ]
            subprocess.run(command, check=True, cwd=ROOT)
            result = subprocess.run(
                [str(executable)],
                check=True,
                cwd=ROOT,
                text=True,
                capture_output=True,
                timeout=30,
            )

        self.assertIn(
            "HAL_EXTERNAL_LINK_OK boundaries=4 depth=8 idle=3 uart_irq=8 "
            "i2c_continuous=2 i2c_stop=1 i2c_abort=1",
            result.stdout,
        )

    def test_bridge_object_has_no_hardware_dependencies(self) -> None:
        compiler = self.compiler()
        bridge = (
            ROOT / "firmware" / "src" / "adapters" / "micropython" /
            "micropython_capability_bridge.c"
        )
        legacy = (
            ROOT / "firmware" / "src" / "services" /
            "micropython_binding_service.c"
        )
        source = bridge.read_text(encoding="utf-8").lower()
        self.assertFalse(legacy.exists())
        for forbidden in (
            "hal_external", "hal_time", "hk_board", "<i2c.h>",
            "<uart.h>", "<plic.h>", "../drivers/", "../../drivers/",
            "i2c_device_", "uart_device_",
        ):
            self.assertNotIn(forbidden, source)
        with tempfile.TemporaryDirectory(prefix="hackylens-mpy-bridge-") as temp:
            object_file = Path(temp) / "micropython_capability_bridge.o"
            subprocess.run([
                compiler,
                "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                f"-I{ROOT / 'firmware' / 'include'}",
                f"-I{ROOT / 'firmware' / 'src' / 'services'}",
                f"-I{ROOT / 'firmware' / 'src' / 'adapters' / 'micropython'}",
                f"-I{ROOT / 'firmware' / 'src' / 'capabilities'}",
                f"-I{ROOT / 'firmware' / 'src' / 'config'}",
                f"-I{ROOT / 'firmware' / 'assets'}",
                "-c", str(bridge), "-o", str(object_file),
            ], check=True, cwd=ROOT)
            nm = shutil.which("nm")
            if nm:
                symbols = subprocess.run(
                    [nm, "-u", str(object_file)], check=True,
                    text=True, capture_output=True,
                ).stdout.lower().split()
                for symbol in symbols:
                    normalized = symbol.lstrip("_")
                    self.assertFalse(normalized.startswith(
                        ("hal_", "hk_board", "i2c_", "uart_", "plic_")
                    ))

    def test_k210_display_adapter_composition_cancel_and_cleanup(self) -> None:
        compiler = self.compiler()

        with tempfile.TemporaryDirectory(prefix="hackylens-display-") as temp:
            executable = Path(temp) / (
                "k210_display_test.exe" if os.name == "nt" else "k210_display_test"
            )
            command = [
                compiler,
                "-std=c11",
                "-O1",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-DK210_DISPLAY_ADAPTER_TESTING",
                f"-I{ROOT / 'firmware' / 'include'}",
                f"-I{ROOT / 'tests'}",
                f"-I{ROOT / 'firmware' / 'src' / 'capabilities'}",
                f"-I{ROOT / 'firmware' / 'src' / 'drivers'}",
                f"-I{ROOT / 'firmware' / 'src' / 'ui'}",
                f"-I{ROOT / 'firmware' / 'src' / 'config'}",
                f"-I{ROOT / 'boards' / 'huskylens-sen0305' / 'generated'}",
                f"-I{ROOT / 'firmware' / 'src' / 'core'}",
                f"-I{ROOT / 'platforms' / 'k210' / 'hal'}",
                f"-I{ROOT / 'firmware' / 'assets'}",
                str(ROOT / "tests" / "k210_display_harness.c"),
                str(ROOT / "tests" / "display_normative_suite.c"),
                str(ROOT / "platforms" / "k210" / "capabilities" /
                    "display_adapter.c"),
                str(ROOT / "firmware" / "src" / "services" /
                    "frame_pool.c"),
                str(ROOT / "firmware" / "src" / "capabilities" /
                    "display.c"),
                str(ROOT / "firmware" / "src" / "ui" / "hk_font.c"),
                "-o",
                str(executable),
            ]
            subprocess.run(command, check=True, cwd=ROOT)
            result = subprocess.run(
                [str(executable)],
                check=True,
                cwd=ROOT,
                text=True,
                capture_output=True,
                timeout=30,
            )

        self.assertIn(
            "K210_DISPLAY_ADAPTER_OK cases=9 normative=7 public_api=1 "
            "slice=128 framebuffer=153600 stream=one-per-region",
            result.stdout,
        )


if __name__ == "__main__":
    unittest.main()
