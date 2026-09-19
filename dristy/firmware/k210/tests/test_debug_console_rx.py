import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class DebugConsoleRxTests(unittest.TestCase):
    @staticmethod
    def compiler() -> str:
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            raise unittest.SkipTest("host C compiler not installed")
        return compiler

    def test_interrupt_ring_with_fake_uart(self) -> None:
        compiler = self.compiler()

        with tempfile.TemporaryDirectory(prefix="dristy-debug-rx-") as temp:
            executable = Path(temp) / (
                "debug_console_rx_test.exe"
                if os.name == "nt"
                else "debug_console_rx_test"
            )
            command = [
                compiler,
                "-std=c11",
                "-O1",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-DDEBUG_CONSOLE_RX_TESTING",
                f"-I{ROOT / 'tests'}",
                f"-I{ROOT / 'platforms' / 'k210' / 'hal'}",
                f"-I{ROOT / 'firmware' / 'src' / 'services'}",
                str(ROOT / "tests" / "debug_console_rx_harness.c"),
                str(ROOT / "platforms" / "k210" / "hal" / "hal_uart.c"),
                str(
                    ROOT
                    / "firmware"
                    / "src"
                    / "services"
                    / "debug_console_service.c"
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
            "DEBUG_CONSOLE_RX_OK stale=5 exact=22+1050 wrap=1100 "
            "overflow=37 ring=2048 irq_init=1 stdout=1",
            result.stdout,
        )

    def test_rx_starts_before_boot_splash(self) -> None:
        startup = (
            ROOT / "firmware" / "src" / "runtime" / "firmware_startup.c"
        ).read_text(encoding="utf-8")
        rx_start = startup.index("debug_console_start_rx();")
        boot = startup.index("boot_controller_show_boot_screen();")
        self.assertLess(rx_start, boot)
        self.assertIn("debug_uart_tick();", startup)


if __name__ == "__main__":
    unittest.main()
