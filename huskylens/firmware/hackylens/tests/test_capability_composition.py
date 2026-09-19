import hashlib
import json
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
import firmware_attestation
import gen_capability_inventory as generator


def request(*, optional: bool = False, minimum: str = "0.1.0",
            maximum: str = "0.2.0", features: tuple[str, ...] = ()) -> str:
    fallback = ', fallback = "legacy-path"' if optional else ""
    encoded_features = ", ".join(f'"{item}"' for item in features)
    return (
        "{ id = \"hackylens.cap.time\", instance = 0, "
        f"minimum = \"{minimum}\", maximum_exclusive = \"{maximum}\", "
        f"features = [{encoded_features}]{fallback} }}"
    )


class CapabilityCompositionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.runtime = board_contract.load_board("huskylens-sen0305")
        cls.cube = board_contract.load_board("sipeed-maix-cube")

    def fixture(self, directory: str, *, required: str = "",
                optional: str = "", provider_symbol: bool = True,
                catalog_suffix: str = "", routes: str = "[]") -> tuple[Path, Path, Path, Path]:
        root = Path(directory)
        source = root / "platforms" / "k210" / "capabilities" / "time_adapter.c"
        source.parent.mkdir(parents=True)
        source.write_text(
            "#include \"capability_provider.h\"\n" +
            ("const hk_capability_provider_t hk_test_time_provider = "
             "{.max_leases = 16U};\n"
             if provider_symbol else "const int wrong_symbol = 0;\n"),
            encoding="utf-8",
        )
        catalog = root / "capabilities.toml"
        catalog_text = """schema = 1
platform = "kendryte-k210"
[[capabilities]]
id = "hackylens.cap.time"
numeric_id = 65537
instance = 0
version = "0.1.0"
feature_bits = { monotonic-us = 0, sleep-until = 1 }
limits = { max-sleep-us = { key = 1, value = 300000000 } }
flags = ["shared"]
affinity = "any"
resources = ["processor"]
routes = []
provider_source = "platforms/k210/capabilities/time_adapter.c"
provider_symbol = "hk_test_time_provider"
max_leases = 16
"""
        catalog_text = catalog_text.replace("routes = []", f"routes = {routes}")
        catalog.write_text(
            catalog_text + catalog_suffix,
            encoding="utf-8",
        )
        apps = root / "apps.toml"
        apps.write_text(
            "schema = 2\n[apps.test]\nrequires = []\n"
            f"required_capabilities = [{required}]\n"
            f"optional_capabilities = [{optional}]\n",
            encoding="utf-8",
        )
        consumers = root / "consumers.toml"
        consumers.write_text(
            "schema = 1\n[consumers.runtime]\nkind = \"runtime\"\n"
            "required_capabilities = []\noptional_capabilities = []\n",
            encoding="utf-8",
        )
        return root, catalog, apps, consumers

    def compose_fixture(self, fixture: tuple[Path, Path, Path, Path],
                        *, required_apps: set[str] | None = None,
                        disabled_capabilities: set[str] | None = None):
        root, catalog, apps, consumers = fixture
        return generator.compose(
            self.runtime, {"test"}, set(), required_apps or set(),
            disabled_capabilities or set(), root=root, catalog_path=catalog,
            app_requirements_path=apps,
            consumer_requirements_path=consumers,
        )

    def test_repository_generation_is_deterministic_and_hash_bound(self) -> None:
        apps = set(generator.load_app_requirements())
        first = generator.compose(self.runtime, apps, set(), set(), set())
        second = generator.compose(self.runtime, apps, set(), set(), set())
        self.assertEqual(generator.generated_c(first), generator.generated_c(second))
        with tempfile.TemporaryDirectory() as first_dir, tempfile.TemporaryDirectory() as second_dir:
            first_caps, first_comp = generator.write_artifacts(first, Path(first_dir))
            second_caps, second_comp = generator.write_artifacts(second, Path(second_dir))
            self.assertEqual(first_caps.read_bytes(), second_caps.read_bytes())
            self.assertEqual(first_comp.read_bytes(), second_comp.read_bytes())
            document = json.loads(first_comp.read_text(encoding="utf-8"))
            self.assertEqual(document["schema"], 2)
            self.assertEqual(
                document["capabilities"]["sha256"],
                hashlib.sha256(first_caps.read_bytes()).hexdigest(),
            )

    def test_duplicate_unknown_id_and_provider_symbol_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = self.fixture(
                directory,
                catalog_suffix="""
[[capabilities]]
id = "hackylens.cap.other"
numeric_id = 65537
instance = 0
version = "0.1.0"
feature_bits = {}
limits = {}
flags = ["shared"]
affinity = "any"
resources = ["processor"]
routes = []
provider_source = "platforms/k210/capabilities/time_adapter.c"
provider_symbol = "hk_other_provider"
max_leases = 1
""",
            )
            with self.assertRaisesRegex(generator.CapabilityError, "duplicate numeric"):
                generator.load_catalog(fixture[1], root=fixture[0])
        with tempfile.TemporaryDirectory() as directory:
            fixture = self.fixture(directory, required=request().replace(
                "hackylens.cap.time", "hackylens.cap.unknown"
            ))
            with self.assertRaisesRegex(generator.CapabilityError, "unknown capability"):
                self.compose_fixture(fixture)
        with tempfile.TemporaryDirectory() as directory:
            fixture = self.fixture(directory, provider_symbol=False)
            with self.assertRaisesRegex(generator.CapabilityError, "provider symbol"):
                self.compose_fixture(fixture)

    def test_version_feature_required_and_optional_resolution(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            available = self.compose_fixture(self.fixture(
                directory, required=request(features=("monotonic-us",))
            ))
            self.assertEqual(len(available.capabilities), 1)
            self.assertEqual(len(available.grants["test"]), 1)
            self.assertIn("hk_test_time_provider", generator.generated_c(available))
            compiler = shutil.which("gcc") or shutil.which("cc")
            self.assertIsNotNone(compiler)
            generated = Path(directory) / "capability_inventory_generated.c"
            generated.write_text(generator.generated_c(available), encoding="utf-8")
            subprocess.run([
                str(compiler), "-std=c11", "-Wall", "-Wextra", "-Werror",
                f"-I{ROOT / 'firmware' / 'include'}",
                f"-I{ROOT / 'firmware' / 'src' / 'capabilities'}",
                "-fsyntax-only", str(generated),
            ], check=True, cwd=ROOT)
        with tempfile.TemporaryDirectory() as directory:
            incompatible = self.compose_fixture(self.fixture(
                directory, required=request(minimum="0.2.0", maximum="0.3.0")
            ))
            self.assertIn("test", incompatible.disabled_apps)
            self.assertEqual(incompatible.exclusions[0]["code"], "version-incompatible")
        with tempfile.TemporaryDirectory() as directory:
            missing_feature = self.compose_fixture(self.fixture(
                directory, required=request(features=("future-feature",))
            ))
            self.assertEqual(missing_feature.exclusions[0]["code"], "feature-missing")
        with tempfile.TemporaryDirectory() as directory:
            optional = self.compose_fixture(self.fixture(
                directory, optional=request(optional=True, features=("future-feature",))
            ))
            self.assertNotIn("test", optional.disabled_apps)
            self.assertEqual(optional.optional_fallbacks[0]["fallback"], "legacy-path")
        with tempfile.TemporaryDirectory() as directory:
            route_missing = self.compose_fixture(self.fixture(
                directory, required=request(), routes='["missing-route"]'
            ))
            self.assertEqual(route_missing.exclusions[0]["code"], "route-unavailable")

    def test_required_app_failure_diagnostic_disable_and_cube_claims(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = self.fixture(
                directory, required=request(features=("future-feature",))
            )
            with self.assertRaisesRegex(generator.CapabilityError, "required app"):
                self.compose_fixture(fixture, required_apps={"test"})
        with tempfile.TemporaryDirectory() as directory:
            fixture = self.fixture(directory, required=request())
            disabled = self.compose_fixture(
                fixture, disabled_capabilities={"hackylens.cap.time"}
            )
            self.assertEqual(disabled.absences[0]["code"], "provider-excluded")
            self.assertIn("test", disabled.disabled_apps)
            image = Path(directory) / "firmware.bin"
            image.write_bytes(b"diagnostic image")
            attestation = firmware_attestation.build_document(
                image,
                self.runtime,
                target="full",
                disabled_apps=set(),
                exclusions=[],
                disabled_capabilities={"hackylens.cap.time"},
                capabilities_sha256="a" * 64,
            )
            self.assertEqual(
                attestation["build_profile"],
                firmware_attestation.FEATURE_BUILD_PROFILE,
            )
            self.assertFalse(attestation["release_qualified"])
        apps = set(generator.load_app_requirements())
        with self.assertRaisesRegex(
            generator.CapabilityError, "required capability consumer"
        ):
            generator.compose(
                self.runtime, apps, set(), set(), {"hackylens.cap.time"}
            )
        diagnostic = generator.compose(
            self.runtime, apps, set(), set(), {"hackylens.cap.time"},
            allow_required_consumer_exclusion=True,
        )
        self.assertEqual(
            [item["consumer"] for item in diagnostic.required_consumer_exclusions],
            ["consumer:external-link-service", "consumer:firmware-runtime"],
        )
        self.assertNotIn("consumer:external-link-service", diagnostic.grants)
        self.assertNotIn("consumer:firmware-runtime", diagnostic.grants)
        runtime = generator.compose(self.runtime, apps, set(), set(), set())
        cube = generator.compose(self.cube, apps, set(), set(), set())
        self.assertEqual(
            [item.id for item in runtime.capabilities],
            ["hackylens.cap.time", "hackylens.cap.input",
             "hackylens.cap.display", "hackylens.cap.external-link",
             "hackylens.cap.lights"],
        )
        self.assertEqual(
            [item.id for item in cube.capabilities],
            ["hackylens.cap.time"],
        )
        self.assertTrue(generator.capabilities_document(runtime)["runtime_supported"])
        self.assertFalse(generator.capabilities_document(cube)["runtime_supported"])
        self.assertTrue(all(item["code"] in generator.ABSENCE_CODES
                            for item in cube.absences))


if __name__ == "__main__":
    unittest.main()
