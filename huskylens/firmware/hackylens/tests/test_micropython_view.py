import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class MicroPythonViewTests(unittest.TestCase):
    def test_bounded_atomic_dirty_region_rendering(self) -> None:
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            raise unittest.SkipTest("host C compiler not installed")

        with tempfile.TemporaryDirectory(prefix="hackylens-micropython-view-") as temp:
            executable = Path(temp) / (
                "micropython_view.exe" if os.name == "nt" else "micropython_view"
            )
            command = [
                compiler,
                "-std=c11",
                "-O1",
                "-Wall",
                "-Wextra",
                "-Werror",
                f"-I{ROOT / 'firmware' / 'src' / 'apps' / 'micropython'}",
                f"-I{ROOT / 'firmware' / 'src' / 'core'}",
                f"-I{ROOT / 'firmware' / 'src' / 'config'}",
                f"-I{ROOT / 'firmware' / 'src' / 'ui'}",
                f"-I{ROOT / 'firmware' / 'src' / 'services'}",
                f"-I{ROOT / 'firmware' / 'src' / 'storage'}",
                f"-I{ROOT / 'firmware' / 'include'}",
                f"-I{ROOT / 'firmware' / 'assets'}",
                str(ROOT / "tests" / "micropython_view_harness.c"),
                str(
                    ROOT
                    / "firmware"
                    / "src"
                    / "apps"
                    / "micropython"
                    / "micropython_view.c"
                ),
                str(ROOT / "firmware" / "src" / "core" / "hk_string.c"),
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
            "MICROPYTHON_VIEW_OK bounds=1 atomic=1 rows=2 regions=2 font=12",
            result.stdout,
        )


if __name__ == "__main__":
    unittest.main()
