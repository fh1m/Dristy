import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class MenuTitleHostTests(unittest.TestCase):
    def test_short_title_clears_long_object_detect_title(self) -> None:
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            raise unittest.SkipTest("host C compiler not installed")

        with tempfile.TemporaryDirectory(prefix="hackylens-menu-title-") as temp:
            executable = Path(temp) / (
                "menu_title_host_test.exe" if os.name == "nt" else "menu_title_host_test"
            )
            command = [
                compiler,
                "-std=c11",
                "-O1",
                "-Wall",
                "-Wextra",
                "-Werror",
                f"-I{ROOT / 'firmware' / 'src' / 'core'}",
                f"-I{ROOT / 'firmware' / 'src' / 'config'}",
                f"-I{ROOT / 'firmware' / 'src' / 'ui'}",
                f"-I{ROOT / 'firmware' / 'src' / 'internal'}",
                f"-I{ROOT / 'firmware' / 'src' / 'capabilities'}",
                f"-I{ROOT / 'firmware' / 'include'}",
                f"-I{ROOT / 'firmware' / 'assets'}",
                f"-I{ROOT / 'boards' / 'huskylens-sen0305' / 'generated'}",
                str(ROOT / "tests" / "menu_title_harness.c"),
                str(ROOT / "firmware" / "src" / "ui" / "ui_menu.c"),
                str(ROOT / "firmware" / "src" / "ui" / "dristy_icons.c"),
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
            "MENU_TITLE_HOST_OK old_object_detect_glyphs_cleared=1",
            result.stdout,
        )


if __name__ == "__main__":
    unittest.main()
