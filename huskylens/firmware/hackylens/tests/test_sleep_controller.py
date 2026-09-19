import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class SleepControllerTests(unittest.TestCase):
    def test_monotonic_request_and_failure_safe_inactivity(self) -> None:
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            raise unittest.SkipTest("host C compiler not installed")

        with tempfile.TemporaryDirectory(prefix="hackylens-sleep-") as temp:
            executable = Path(temp) / (
                "sleep_controller.exe" if os.name == "nt" else "sleep_controller"
            )
            subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-O1",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-Wno-unused-parameter",
                    f"-I{ROOT / 'firmware' / 'include'}",
                    str(ROOT / "tests" / "sleep_controller_harness.c"),
                    str(
                        ROOT
                        / "firmware"
                        / "src"
                        / "apps"
                        / "sleep"
                        / "sleep_controller.c"
                    ),
                    "-o",
                    str(executable),
                ],
                check=True,
                cwd=ROOT,
            )
            result = subprocess.run(
                [str(executable)],
                check=True,
                cwd=ROOT,
                text=True,
                capture_output=True,
                timeout=30,
            )

        self.assertIn(
            "SLEEP_CONTROLLER_OK monotonic_only=1 failure_safe=1 wrap_safe=1",
            result.stdout,
        )


if __name__ == "__main__":
    unittest.main()
