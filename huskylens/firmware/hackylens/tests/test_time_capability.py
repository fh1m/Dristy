import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class TimeCapabilityTests(unittest.TestCase):
    @staticmethod
    def compiler() -> str:
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            raise unittest.SkipTest("host C compiler not installed")
        return compiler

    def run_normative_backend(self, backend: str) -> str:
        compiler = self.compiler()
        with tempfile.TemporaryDirectory(
            prefix=f"hackylens-time-{backend}-"
        ) as temp:
            temporary = Path(temp)
            executable = temporary / (
                "time_capability.exe" if os.name == "nt" else "time_capability"
            )
            common = [
                "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                f"-I{ROOT / 'firmware' / 'include'}",
                f"-I{ROOT / 'firmware' / 'src' / 'capabilities'}",
                f"-I{ROOT / 'firmware' / 'src' / 'runtime'}",
                f"-I{ROOT / 'tests'}",
            ]
            sources = [
                ROOT / "tests" / "time_capability_harness.c",
                ROOT / "tests" / f"time_normative_{backend}_backend.c",
                ROOT / "firmware" / "src" / "capabilities" / "time.c",
                ROOT / "firmware" / "src" / "capabilities" /
                "capability_core.c",
            ]
            if backend == "k210":
                common.append(f"-I{ROOT / 'tests' / 'k210_time_adapter_stubs'}")
                sources.append(
                    ROOT / "platforms" / "k210" / "capabilities" /
                    "time_adapter.c"
                )
            subprocess.run([
                compiler, *common, *(str(source) for source in sources),
                "-o", str(executable),
            ], check=True, cwd=ROOT)
            result = subprocess.run(
                [str(executable)], check=True, cwd=ROOT,
                text=True, capture_output=True, timeout=30,
            )
        return result.stdout

    def test_fake_passes_time_normative_contract_and_bounded_object(self) -> None:
        result = self.run_normative_backend("fake")
        compiler = self.compiler()
        with tempfile.TemporaryDirectory(prefix="hackylens-time-object-") as temp:
            time_object = Path(temp) / "time.o"
            subprocess.run([
                compiler, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                f"-I{ROOT / 'firmware' / 'include'}",
                f"-I{ROOT / 'firmware' / 'src' / 'capabilities'}",
                "-c",
                str(ROOT / "firmware" / "src" / "capabilities" / "time.c"),
                "-o", str(time_object),
            ], check=True, cwd=ROOT)
            nm = shutil.which("nm")
            if nm:
                symbols = subprocess.run(
                    [nm, "-u", str(time_object)], check=True,
                    text=True, capture_output=True,
                ).stdout.lower()
                for forbidden in ("malloc", "calloc", "realloc", "free", "task", "queue"):
                    self.assertNotIn(forbidden, symbols)

        self.assertIn(
            "TIME_NORMATIVE_OK backend=fake cases=10 max_slice_us=5000",
            result,
        )

    def test_k210_passes_same_time_normative_contract(self) -> None:
        result = self.run_normative_backend("k210")
        self.assertIn(
            "TIME_NORMATIVE_OK backend=k210 cases=10 max_slice_us=5000",
            result,
        )

    def test_k210_any_core_clock_read_is_inside_provider_lock(self) -> None:
        compiler = self.compiler()
        with tempfile.TemporaryDirectory(prefix="hackylens-k210-time-") as temp:
            executable = Path(temp) / (
                "k210_time_adapter.exe" if os.name == "nt" else "k210_time_adapter"
            )
            subprocess.run([
                compiler,
                "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                f"-I{ROOT / 'tests' / 'k210_time_adapter_stubs'}",
                f"-I{ROOT / 'firmware' / 'include'}",
                str(ROOT / "tests" / "k210_time_adapter_harness.c"),
                str(ROOT / "platforms" / "k210" / "capabilities" / "time_adapter.c"),
                "-o", str(executable),
            ], check=True, cwd=ROOT)
            result = subprocess.run(
                [str(executable)], check=True, cwd=ROOT,
                text=True, capture_output=True, timeout=30,
            )

        self.assertIn(
            "K210_TIME_ANY_CORE_ORDER_OK reads=2 locks=2", result.stdout,
        )

    def test_all_application_time_calls_use_the_public_provider(self) -> None:
        apps = ROOT / "firmware" / "src" / "apps"
        sources = "\n".join(
            path.read_text(encoding="utf-8")
            for path in sorted(apps.rglob("*"))
            if path.suffix in {".c", ".h"}
        )
        self.assertNotIn("time_internal", sources)
        self.assertNotIn("hal_time_us", sources)
        self.assertNotIn("hal_sleep", sources)

        bindings = (
            apps / "micropython" / "micropython_bindings.c"
        ).read_text(encoding="utf-8")
        self.assertIn("hk_time_now_us", bindings)
        self.assertIn("hk_time_sleep_until", bindings)
        self.assertIn("micropython_runtime_interrupt_pending", bindings)
        self.assertIn(
            "mp_obj_new_int_from_uint((mp_uint_t)(now / 1000ULL))",
            bindings,
        )
        self.assertNotIn("mp_obj_new_int_from_ull(now / 1000ULL)", bindings)

        adapters = list((ROOT / "platforms").rglob("time_adapter.c"))
        self.assertEqual(
            [ROOT / "platforms" / "k210" / "capabilities" / "time_adapter.c"],
            adapters,
        )


if __name__ == "__main__":
    unittest.main()
