import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class MicroPythonAppTests(unittest.TestCase):
    def test_functional_script_manager(self) -> None:
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            raise unittest.SkipTest("host C compiler not installed")

        with tempfile.TemporaryDirectory(prefix="hackylens-micropython-app-") as temp:
            executable = Path(temp) / (
                "micropython_app.exe" if os.name == "nt" else "micropython_app"
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
                    str(ROOT / "tests" / "micropython_app_harness.c"),
                    str(
                        ROOT
                        / "firmware"
                        / "src"
                        / "apps"
                        / "micropython"
                        / "micropython_app.c"
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
            "MICROPYTHON_APP_OK list=1 preview=1 actions=1 run=1 stop=1 logs=1",
            result.stdout,
        )


if __name__ == "__main__":
    unittest.main()
