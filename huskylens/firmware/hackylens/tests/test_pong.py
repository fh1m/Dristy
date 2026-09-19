import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class PongHostTests(unittest.TestCase):
    @staticmethod
    def compiler() -> str:
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            raise unittest.SkipTest("host C compiler not installed")
        return compiler

    def test_fixed_step_physics_and_dirty_rendering(self) -> None:
        compiler = self.compiler()

        with tempfile.TemporaryDirectory(prefix="hackylens-pong-") as temp:
            executable = Path(temp) / (
                "pong_host_test.exe" if os.name == "nt" else "pong_host_test"
            )
            command = [
                compiler,
                "-std=c11",
                "-O1",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-DPONG_HOST_TESTING",
                f"-I{ROOT / 'firmware' / 'src' / 'apps' / 'pong'}",
                f"-I{ROOT / 'firmware' / 'src' / 'core'}",
                f"-I{ROOT / 'firmware' / 'src' / 'config'}",
                f"-I{ROOT / 'firmware' / 'src' / 'drivers'}",
                f"-I{ROOT / 'firmware' / 'src' / 'internal'}",
                f"-I{ROOT / 'firmware' / 'src' / 'capabilities'}",
                f"-I{ROOT / 'firmware' / 'include'}",
                f"-I{ROOT / 'firmware' / 'src' / 'ui'}",
                f"-I{ROOT / 'firmware' / 'assets'}",
                f"-I{ROOT / 'boards' / 'huskylens-sen0305' / 'generated'}",
                str(ROOT / "tests" / "pong_harness.c"),
                str(
                    ROOT
                    / "firmware"
                    / "src"
                    / "apps"
                    / "pong"
                    / "pong_controller.c"
                ),
                str(
                    ROOT
                    / "firmware"
                    / "src"
                    / "apps"
                    / "pong"
                    / "pong_view.c"
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
            "PONG_HOST_OK fixed_step=20ms dirty_regions=bounded presents=1",
            result.stdout,
        )


if __name__ == "__main__":
    unittest.main()
