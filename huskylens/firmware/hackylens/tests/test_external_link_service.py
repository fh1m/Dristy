import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ExternalLinkServiceTests(unittest.TestCase):
    def test_native_service_uses_capability_and_restores_transport(self) -> None:
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            raise unittest.SkipTest("host C compiler not installed")
        with tempfile.TemporaryDirectory(prefix="hackylens-external-service-") as temp:
            executable = Path(temp) / (
                "external_link_service.exe" if os.name == "nt"
                else "external_link_service"
            )
            subprocess.run([
                compiler,
                "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                f"-I{ROOT / 'firmware' / 'include'}",
                f"-I{ROOT / 'firmware' / 'src' / 'services'}",
                f"-I{ROOT / 'firmware' / 'src' / 'capabilities'}",
                f"-I{ROOT / 'firmware' / 'src' / 'core'}",
                f"-I{ROOT / 'boards' / 'huskylens-sen0305' / 'generated'}",
                str(ROOT / "tests" / "external_link_service_harness.c"),
                str(ROOT / "firmware" / "src" / "services" /
                    "external_link_service.c"),
                str(ROOT / "firmware" / "src" / "services" /
                    "external_link_protocol.c"),
                "-o", str(executable),
            ], check=True, cwd=ROOT)
            result = subprocess.run(
                [str(executable)], check=True, cwd=ROOT,
                text=True, capture_output=True, timeout=30,
            )
        self.assertIn(
            "EXTERNAL_LINK_SERVICE_OK protocol=v1 reacquire=1 "
            "uart_async=1 target=1",
            result.stdout,
        )


if __name__ == "__main__":
    unittest.main()
