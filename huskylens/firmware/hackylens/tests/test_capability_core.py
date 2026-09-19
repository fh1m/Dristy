import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
CORE_SOURCES = (
    ROOT / "firmware" / "src" / "capabilities" / "capability_core.c",
    ROOT / "firmware" / "src" / "capabilities" / "capability_owner.c",
    ROOT / "firmware" / "src" / "runtime" / "capability_owner_runtime.c",
    ROOT / "tests" / "capability_inventory_generated_stub.c",
)


class CapabilityCoreHostTests(unittest.TestCase):
    @staticmethod
    def compiler() -> str:
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            raise RuntimeError("host C compiler is required")
        return compiler

    @staticmethod
    def include_args() -> list[str]:
        return [
            f"-I{ROOT / 'firmware' / 'include'}",
            f"-I{ROOT / 'firmware' / 'src' / 'capabilities'}",
            f"-I{ROOT / 'firmware' / 'src' / 'core'}",
            f"-I{ROOT / 'firmware' / 'src' / 'runtime'}",
            f"-I{ROOT / 'tests'}",
        ]

    def test_common_lifecycle_contract_suite(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hackylens-capability-") as temp:
            executable = Path(temp) / (
                "capability_contract.exe" if os.name == "nt"
                else "capability_contract"
            )
            command = [
                self.compiler(),
                "-std=c11",
                "-O1",
                "-Wall",
                "-Wextra",
                "-Werror",
                *self.include_args(),
                str(ROOT / "tests" / "capability_contract_suite.c"),
                str(ROOT / "tests" / "capability_fake_provider.c"),
                *(str(path) for path in CORE_SOURCES),
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
            "CAPABILITY_CONTRACT_OK owners=16 leases=32 providers=16",
            result.stdout,
        )

    def test_core_objects_have_no_heap_task_or_queue_symbols(self) -> None:
        compiler = self.compiler()
        nm = shutil.which("nm")
        if not nm:
            compiler_dir = Path(compiler).resolve().parent
            nm_path = compiler_dir / ("nm.exe" if os.name == "nt" else "nm")
            if nm_path.is_file():
                nm = str(nm_path)
        self.assertIsNotNone(nm, "host nm is required for symbol validation")
        forbidden = (
            "malloc", "calloc", "realloc", "free",
            "xtaskcreate", "xqueue", "queuecreate", "pthread_create",
        )
        with tempfile.TemporaryDirectory(prefix="hackylens-capability-obj-") as temp:
            objects: list[Path] = []
            for source in CORE_SOURCES:
                target = Path(temp) / f"{source.stem}.o"
                subprocess.run(
                    [
                        compiler,
                        "-std=c11",
                        "-O1",
                        "-Wall",
                        "-Wextra",
                        "-Werror",
                        *self.include_args(),
                        "-c",
                        str(source),
                        "-o",
                        str(target),
                    ],
                    check=True,
                    cwd=ROOT,
                )
                objects.append(target)
            result = subprocess.run(
                [str(nm), "-u", *(str(path) for path in objects)],
                check=True,
                cwd=ROOT,
                text=True,
                capture_output=True,
            )
        symbols = result.stdout.casefold()
        for token in forbidden:
            with self.subTest(token=token):
                self.assertNotIn(token, symbols)


if __name__ == "__main__":
    unittest.main()
