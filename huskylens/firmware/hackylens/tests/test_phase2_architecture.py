from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import check_arch


class Phase2ArchitectureGuardTests(unittest.TestCase):
    def test_explicit_policy_classifies_every_production_source(self) -> None:
        policy = check_arch.load_layer_policy()
        unclassified = [
            check_arch.repository_relative(path)
            for path in check_arch.repository_source_files(policy)
            if check_arch.classify_repository_path(
                check_arch.repository_relative(path), policy
            ) is None
        ]
        self.assertEqual(unclassified, [])
        self.assertEqual(
            check_arch.classify_repository_path(
                r"FIRMWARE\SRC\APPS\demo\drivers\misleading.h", policy
            ),
            "app",
        )

    def test_phase2_negative_edges_are_hard_rules(self) -> None:
        cases = (
            ("firmware/src/apps/demo/app.c", "firmware/src/drivers/device.h"),
            ("firmware/src/apps/demo/app.c", "boards/example/board.c"),
            ("firmware/src/adapters/micropython/binding.c",
             "platforms/k210/hal/hal_i2c.h"),
            ("platforms/k210/capabilities/display_adapter.c",
             "firmware/src/apps/files/files_app.h"),
            ("firmware/src/services/shared.c",
             "firmware/src/apps/camera/camera_private.h"),
            ("boards/example/board.c", "firmware/src/ui/hk_ui.h"),
        )
        for source, target in cases:
            with self.subTest(source=source, target=target):
                self.assertIsNotNone(
                    check_arch.layer_edge_violation(source, target)
                )
        self.assertIsNone(check_arch.layer_edge_violation(
            "firmware/src/apps/demo/app.c",
            "firmware/include/hackylens/capability/display.h",
        ))

    def test_forwarding_headers_and_symlink_targets_do_not_hide_drivers(self) -> None:
        graph = {
            "firmware/src/apps/demo/app.c": ["firmware/src/core/forward.h"],
            "firmware/src/core/forward.h": ["firmware/src/drivers/private.h"],
            "firmware/src/drivers/private.h": [],
        }
        failures = check_arch.transitive_layer_failures(graph)
        self.assertTrue(any("transitive dependency" in item for item in failures))

        with tempfile.TemporaryDirectory(prefix="hackylens-arch-symlink-") as temp:
            repository = Path(temp)
            app = repository / "firmware" / "src" / "apps" / "demo" / "app.c"
            driver = repository / "firmware" / "src" / "drivers" / "private.h"
            app.parent.mkdir(parents=True)
            driver.parent.mkdir(parents=True)
            app.write_text('#include "forward.h"\n', encoding="utf-8")
            driver.write_text("#pragma once\n", encoding="utf-8")
            forward = app.parent / "forward.h"
            forward.write_text("placeholder\n", encoding="utf-8")
            real_resolve = Path.resolve

            def resolve_with_link(path: Path, *args, **kwargs):
                if path == forward:
                    return real_resolve(driver)
                return real_resolve(path, *args, **kwargs)

            with mock.patch.object(Path, "resolve", resolve_with_link):
                target = check_arch.resolve_repository_include(
                    app, "forward.h", repository_root=repository,
                    source_root=repository / "firmware" / "src",
                )
            self.assertEqual(target, "firmware/src/drivers/private.h")

    def test_generated_dependencies_and_object_symbols_are_policy_inputs(self) -> None:
        dependency = (
            "target.obj: C:\\stage\\firmware\\src\\apps\\demo\\app.c \\\n"
            " C:\\stage\\firmware\\src\\drivers\\private.h\n"
        )
        self.assertEqual(
            check_arch.dependency_repository_paths(dependency),
            [
                "firmware/src/apps/demo/app.c",
                "firmware/src/drivers/private.h",
            ],
        )
        self.assertEqual(
            check_arch.forbidden_hardware_symbols(
                {"hal_hidden_call", "driver_private", "hk_display_present"},
                {"driver_private"},
            ),
            ["driver_private", "hal_hidden_call"],
        )

    def test_object_symbol_graph_proves_every_forbidden_layer_family(self) -> None:
        def record(
            source: str, *, defined: tuple[str, ...] = (),
            undefined: tuple[str, ...] = (), suffix: str = "",
        ) -> check_arch.RepositoryObjectSymbols:
            layer = check_arch.classify_repository_path(source)
            self.assertIsNotNone(layer)
            return check_arch.RepositoryObjectSymbols(
                f"objects/{source}{suffix}.obj", source, layer,
                frozenset(defined), frozenset(undefined),
            )

        objects = [
            record(
                "firmware/src/apps/demo/caller.c",
                undefined=("driver_private_hook",),
            ),
            record(
                "firmware/src/adapters/micropython/caller.c",
                undefined=("hal_private_hook",),
            ),
            record(
                "platforms/k210/capabilities/display_adapter.c",
                undefined=("camera_private_hook", "duplicate_hook"),
            ),
            record(
                "firmware/src/services/shared.c",
                undefined=("files_private_hook",),
            ),
            record(
                "boards/example/board.c",
                undefined=("controller_policy_hook", "app_policy_hook"),
            ),
            record(
                "firmware/src/drivers/private.c",
                defined=("driver_private_hook",),
            ),
            record(
                "platforms/k210/hal/hal_private.c",
                defined=("hal_private_hook",),
            ),
            record(
                "firmware/src/apps/camera/private.c",
                defined=("camera_private_hook", "app_policy_hook"),
            ),
            record(
                "firmware/src/apps/files/private.c",
                defined=("files_private_hook",),
            ),
            record(
                "firmware/src/controllers/product_policy.c",
                defined=("controller_policy_hook",),
            ),
            # A duplicate definition in an allowed layer must not mask the
            # forbidden app definition of the same symbol.
            record(
                "firmware/src/core/duplicate.c",
                defined=("duplicate_hook",), suffix="-core",
            ),
            record(
                "firmware/src/apps/camera/duplicate.c",
                defined=("duplicate_hook",), suffix="-app",
            ),
        ]
        failures = check_arch.repository_object_symbol_edge_failures(objects)
        expected = (
            ("driver_private_hook", "app -> driver"),
            ("hal_private_hook", "adapter -> platform-hal"),
            ("camera_private_hook", "capability-implementation -> app"),
            ("files_private_hook", "service -> app"),
            ("controller_policy_hook", "board -> controller"),
            ("app_policy_hook", "board -> app"),
            ("duplicate_hook", "capability-implementation -> app"),
        )
        for symbol, edge in expected:
            with self.subTest(symbol=symbol):
                self.assertTrue(any(
                    symbol in failure and edge in failure
                    for failure in failures
                ), failures)
        self.assertEqual(
            failures,
            check_arch.repository_object_symbol_edge_failures(
                list(reversed(objects))
            ),
        )

        external = record(
            "firmware/src/apps/demo/external.c",
            undefined=("hal_external_only",),
        )
        self.assertTrue(any(
            "forbidden undefined hardware symbol hal_external_only" in failure
            for failure in check_arch.repository_object_symbol_edge_failures(
                [external]
            )
        ))

        # Exercise build-object discovery too, so a future origin-layer filter
        # in object_undefined_symbol_failures cannot bypass the generic graph.
        fixture_symbols = {
            "firmware/src/apps/demo/caller.c": (
                (), ("driver_private_hook",),
            ),
            "firmware/src/adapters/micropython/caller.c": (
                (), ("hal_private_hook",),
            ),
            "platforms/k210/capabilities/display_adapter.c": (
                (), ("camera_private_hook",),
            ),
            "firmware/src/services/shared.c": (
                (), ("files_private_hook",),
            ),
            "boards/example/board.c": (
                (), ("controller_policy_hook", "app_policy_hook"),
            ),
            "firmware/src/drivers/private.c": (
                ("driver_private_hook",), (),
            ),
            "platforms/k210/hal/hal_private.c": (
                ("hal_private_hook",), (),
            ),
            "firmware/src/apps/camera/private.c": (
                ("camera_private_hook", "app_policy_hook"), (),
            ),
            "firmware/src/apps/files/private.c": (
                ("files_private_hook",), (),
            ),
            "firmware/src/controllers/product_policy.c": (
                ("controller_policy_hook",), (),
            ),
        }
        with tempfile.TemporaryDirectory(
            prefix="hackylens-object-symbols-"
        ) as temp:
            build_dir = Path(temp)
            for source in fixture_symbols:
                path = build_dir / "objects" / f"{source}.obj"
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"object fixture")

            def fixture_object_symbols(
                _nm: str, path: Path, *, undefined: bool,
            ) -> set[str]:
                source = check_arch.object_repository_source(path)
                self.assertIn(source, fixture_symbols)
                defined, unresolved = fixture_symbols[source]
                return set(unresolved if undefined else defined)

            with (
                mock.patch.object(check_arch, "find_nm", return_value="nm"),
                mock.patch.object(
                    check_arch, "object_symbols",
                    side_effect=fixture_object_symbols,
                ),
            ):
                discovered = check_arch.object_undefined_symbol_failures(
                    build_dir
                )
        for symbol, edge in expected[:6]:
            with self.subTest(discovered_symbol=symbol):
                self.assertTrue(any(
                    symbol in failure and edge in failure
                    for failure in discovered
                ), discovered)

    def test_inventory_and_python_provider_bypasses_are_detected(self) -> None:
        manual = (
            "static const hk_capability_provider_t *const providers[] = {0};\n"
            "void hk_generated_capability_inventory_get(void) { }\n"
        )
        self.assertEqual(check_arch.manual_provider_inventory_lines(manual), [1, 2])
        self.assertEqual(
            check_arch.python_gated_provider_lines(
                "#if HK_ENABLE_APP_MICROPYTHON\n#endif\n"
            ),
            [1],
        )
        self.assertEqual(
            check_arch.provider_hash_mismatches(
                {"platforms/k210/capabilities/display_adapter.c": "a"},
                {"platforms/k210/capabilities/display_adapter.c": "b"},
            ),
            [
                "platforms/k210/capabilities/display_adapter.c: provider object "
                "differs between full and MicroPython-disabled profiles"
            ],
        )

    def test_frame_pool_borrow_release_contract(self) -> None:
        compiler = os.environ.get("CC") or "gcc"
        with tempfile.TemporaryDirectory(prefix="hackylens-frame-pool-") as temp:
            executable = Path(temp) / (
                "frame_pool.exe" if os.name == "nt" else "frame_pool"
            )
            subprocess.run(
                [
                    compiler, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                    "-DHK_FRAME_POOL_TESTING",
                    f"-I{ROOT / 'firmware' / 'src' / 'services'}",
                    f"-I{ROOT / 'firmware' / 'src' / 'config'}",
                    f"-I{ROOT / 'boards' / 'huskylens-sen0305' / 'generated'}",
                    str(ROOT / "tests" / "frame_pool_harness.c"),
                    str(ROOT / "firmware" / "src" / "services" / "frame_pool.c"),
                    "-o", str(executable),
                ],
                cwd=ROOT, check=True,
            )
            result = subprocess.run(
                [str(executable)], cwd=ROOT, check=True, text=True,
                capture_output=True, timeout=30,
            )
        self.assertIn(
            "FRAME_POOL_OK borrow=exclusive stale=blocked camera=exclusive "
            "generation=exhausted",
            result.stdout,
        )

    def test_repository_guard_is_green(self) -> None:
        self.assertEqual(check_arch.phase2_source_failures(), [])


if __name__ == "__main__":
    unittest.main()
