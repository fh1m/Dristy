import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class FilesControllerTests(unittest.TestCase):
    def test_ok_opens_with_least_privilege_and_failure_safe_timing(self) -> None:
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            raise unittest.SkipTest("host C compiler not installed")

        with tempfile.TemporaryDirectory(prefix="hackylens-files-") as temp:
            executable = Path(temp) / (
                "files_controller.exe" if os.name == "nt" else "files_controller"
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
                    str(ROOT / "tests" / "files_controller_harness.c"),
                    str(
                        ROOT
                        / "firmware"
                        / "src"
                        / "apps"
                        / "files"
                        / "files_controller.c"
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
            "FILES_CONTROLLER_OK open=1 monotonic=1 failure_safe=1",
            result.stdout,
        )


if __name__ == "__main__":
    unittest.main()
