import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ButtonsAppTests(unittest.TestCase):
    def test_interactive_button_qualification_flow(self) -> None:
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            raise unittest.SkipTest("host C compiler not installed")

        with tempfile.TemporaryDirectory(prefix="hackylens-buttons-") as temp:
            executable = Path(temp) / (
                "buttons_controller.exe" if os.name == "nt" else "buttons_controller"
            )
            subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-O1",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    f"-I{ROOT / 'firmware' / 'include'}",
                    str(ROOT / "tests" / "buttons_controller_harness.c"),
                    str(
                        ROOT
                        / "firmware"
                        / "src"
                        / "apps"
                        / "buttons"
                        / "buttons_controller.c"
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
            "BUTTONS_CONTROLLER_OK press_release=1 hold=1 repeat=1 back=1",
            result.stdout,
        )


if __name__ == "__main__":
    unittest.main()
