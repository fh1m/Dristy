from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class UserFsFaultTests(unittest.TestCase):
    def test_littlefs_atomic_replace_survives_each_mutation_cut(self) -> None:
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            self.skipTest("host C compiler not installed")
        littlefs = ROOT / "_deps" / "littlefs"
        if not (littlefs / "lfs.c").is_file():
            self.skipTest("pinned littlefs checkout is not bootstrapped")

        with tempfile.TemporaryDirectory(prefix="hackylens-userfs-") as temp:
            executable = Path(temp) / ("userfs_test.exe" if os.name == "nt" else "userfs_test")
            command = [
                compiler,
                "-std=c11",
                "-O1",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-DUSERFS_TESTING",
                "-DLFS_NO_MALLOC",
                "-DLFS_NAME_MAX=63",
                "-DLFS_NO_DEBUG",
                "-DLFS_NO_WARN",
                "-DLFS_NO_ERROR",
                "-DLFS_NO_TRACE",
                f"-I{ROOT / 'firmware' / 'src' / 'storage'}",
                f"-I{ROOT / 'firmware' / 'src' / 'core'}",
                f"-I{littlefs}",
                str(ROOT / "tests" / "userfs_harness.c"),
                str(ROOT / "firmware" / "src" / "storage" / "userfs.c"),
                str(ROOT / "firmware" / "src" / "core" / "hk_binary.c"),
                str(littlefs / "lfs.c"),
                str(littlefs / "lfs_util.c"),
                "-o",
                str(executable),
            ]
            subprocess.run(command, check=True, cwd=ROOT)
            result = subprocess.run(
                [str(executable)], check=True, cwd=ROOT,
                text=True, capture_output=True, timeout=120,
            )
            self.assertIn("USERFS_OK", result.stdout)
            self.assertIn("power_cuts=", result.stdout)


if __name__ == "__main__":
    unittest.main()
