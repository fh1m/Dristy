import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import board_contract
import gen_capability_inventory as generator


class LightsCapabilityTests(unittest.TestCase):
    @staticmethod
    def compiler() -> str:
        compiler = shutil.which("gcc") or shutil.which("cc")
        if not compiler:
            raise unittest.SkipTest("host C compiler not installed")
        return compiler

    def run_normative_backend(self, backend: str) -> str:
        compiler = self.compiler()
        with tempfile.TemporaryDirectory(
            prefix=f"hackylens-lights-{backend}-"
        ) as temp:
            temporary = Path(temp)
            executable = temporary / (
                "lights_capability.exe" if os.name == "nt" else "lights_capability"
            )
            common = [
                "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                f"-I{ROOT / 'firmware' / 'include'}",
                f"-I{ROOT / 'firmware' / 'src' / 'capabilities'}",
                f"-I{ROOT / 'tests'}",
            ]
            sources = [
                ROOT / "tests" / "lights_capability_harness.c",
                ROOT / "tests" / f"lights_normative_{backend}_backend.c",
                ROOT / "firmware" / "src" / "capabilities" / "lights.c",
                ROOT / "firmware" / "src" / "capabilities" /
                "capability_core.c",
            ]
            if backend == "k210":
                sources.append(
                    ROOT / "platforms" / "k210" / "capabilities" /
                    "lights_adapter.c"
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

    def test_fake_passes_lights_normative_contract_and_bounded_object(self) -> None:
        result = self.run_normative_backend("fake")
        compiler = self.compiler()
        with tempfile.TemporaryDirectory(prefix="hackylens-lights-object-") as temp:
            lights_object = Path(temp) / "lights.o"
            subprocess.run([
                compiler, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                f"-I{ROOT / 'firmware' / 'include'}",
                f"-I{ROOT / 'firmware' / 'src' / 'capabilities'}",
                "-c",
                str(ROOT / "firmware" / "src" / "capabilities" / "lights.c"),
                "-o", str(lights_object),
            ], check=True, cwd=ROOT)
            nm = shutil.which("nm")
            if nm:
                symbols = subprocess.run(
                    [nm, "-u", str(lights_object)], check=True,
                    text=True, capture_output=True,
                ).stdout.lower()
                for forbidden in ("malloc", "calloc", "realloc", "free", "task", "queue"):
                    self.assertNotIn(forbidden, symbols)

        self.assertIn(
            "LIGHTS_NORMATIVE_OK backend=fake cases=16 effects=10 "
            "safe_off_mask=0x7 level_max=1000",
            result,
        )

    def test_k210_passes_same_lights_normative_contract(self) -> None:
        result = self.run_normative_backend("k210")
        self.assertIn(
            "LIGHTS_NORMATIVE_OK backend=k210 cases=16 effects=10 "
            "safe_off_mask=0x7 level_max=1000",
            result,
        )

    def test_k210_adapter_converts_only_after_cancel_and_deadline_checks(self) -> None:
        compiler = self.compiler()
        with tempfile.TemporaryDirectory(prefix="hackylens-k210-lights-") as temp:
            executable = Path(temp) / (
                "k210_lights.exe" if os.name == "nt" else "k210_lights"
            )
            subprocess.run([
                compiler,
                "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                f"-I{ROOT / 'firmware' / 'include'}",
                f"-I{ROOT / 'firmware' / 'src' / 'capabilities'}",
                f"-I{ROOT / 'firmware' / 'src' / 'drivers'}",
                f"-I{ROOT / 'platforms' / 'k210' / 'hal'}",
                str(ROOT / "tests" / "k210_lights_adapter_harness.c"),
                str(ROOT / "platforms" / "k210" / "capabilities" / "lights_adapter.c"),
                "-o", str(executable),
            ], check=True, cwd=ROOT)
            result = subprocess.run(
                [str(executable)], check=True, cwd=ROOT,
                text=True, capture_output=True, timeout=30,
            )

        self.assertIn(
            "K210_LIGHTS_OK writes=4 illum_percent=51 rgb=100/50/0",
            result.stdout,
        )

    def test_settings_reacquire_replays_latest_values(self) -> None:
        compiler = self.compiler()
        with tempfile.TemporaryDirectory(prefix="hackylens-settings-lights-") as temp:
            executable = Path(temp) / (
                "settings_lights.exe" if os.name == "nt" else "settings_lights"
            )
            subprocess.run([
                compiler,
                "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                f"-I{ROOT / 'firmware' / 'include'}",
                f"-I{ROOT / 'firmware' / 'src' / 'services'}",
                f"-I{ROOT / 'firmware' / 'src' / 'capabilities'}",
                str(ROOT / "tests" / "settings_lights_harness.c"),
                str(ROOT / "firmware" / "src" / "services" / "settings_lights_apply.c"),
                "-o", str(executable),
            ], check=True, cwd=ROOT)
            result = subprocess.run(
                [str(executable)], check=True, cwd=ROOT,
                text=True, capture_output=True, timeout=30,
            )

        self.assertIn(
            "SETTINGS_LIGHTS_OK reacquired=2 latest=700/110/220/330",
            result.stdout,
        )

    def test_composition_and_all_consumers_share_the_provider(self) -> None:
        apps = set(generator.load_app_requirements())
        runtime_board = board_contract.load_board("huskylens-sen0305")
        cube = board_contract.load_board("sipeed-maix-cube")
        runtime = generator.compose(runtime_board, apps, set(), set(), set())
        conformance = generator.compose(cube, apps, set(), set(), set())

        self.assertEqual(
            [item.id for item in runtime.capabilities],
            ["hackylens.cap.time", "hackylens.cap.input",
             "hackylens.cap.display", "hackylens.cap.external-link",
             "hackylens.cap.lights"],
        )
        self.assertEqual(
            [item.id for item in conformance.capabilities],
            ["hackylens.cap.time"],
        )
        disabled = generator.compose(
            runtime_board, apps, set(), set(), {"hackylens.cap.lights"},
        )
        for app in (
            "camera", "qr-camera", "face-detect", "apriltag",
            "object-detect", "settings", "sleep", "micropython",
        ):
            self.assertIn(app, disabled.disabled_apps)
        with self.assertRaisesRegex(generator.CapabilityError, "required app"):
            generator.compose(
                runtime_board, apps, set(), {"settings"},
                {"hackylens.cap.lights"},
            )

        settings = (
            ROOT / "firmware" / "src" / "services" / "settings_lights_apply.c"
        ).read_text(encoding="utf-8")
        camera = (
            ROOT / "firmware" / "src" / "services" / "camera_light_apply.c"
        ).read_text(encoding="utf-8")
        micropython = (
            ROOT
            / "firmware"
            / "src"
            / "adapters"
            / "micropython"
            / "micropython_capability_bridge.c"
        ).read_text(encoding="utf-8")
        sleep = (
            ROOT / "firmware" / "src" / "apps" / "sleep" / "sleep_controller.c"
        ).read_text(encoding="utf-8")
        self.assertIn("hk_lights_set_level", settings + camera + micropython)
        self.assertIn("hk_lights_set_rgb", settings + camera + micropython)
        self.assertIn("screen_brightness_off", sleep)
        self.assertNotIn("drivers/hk_lights.h", micropython)
        self.assertNotIn("lights_illum_set", settings + camera + micropython)
        self.assertNotIn("lights_rgb_set", settings + camera + micropython)


if __name__ == "__main__":
    unittest.main()
