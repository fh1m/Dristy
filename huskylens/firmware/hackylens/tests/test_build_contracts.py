import importlib.util
import datetime
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "hackylens_build_firmware", ROOT / "tools" / "build_firmware.py"
)
assert SPEC is not None and SPEC.loader is not None
BUILD_FIRMWARE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILD_FIRMWARE)
PACKAGE_SPEC = importlib.util.spec_from_file_location(
    "hackylens_package_release", ROOT / "tools" / "package_release.py"
)
assert PACKAGE_SPEC is not None and PACKAGE_SPEC.loader is not None
PACKAGE_RELEASE = importlib.util.module_from_spec(PACKAGE_SPEC)
PACKAGE_SPEC.loader.exec_module(PACKAGE_RELEASE)
HKFLASH_SPEC = importlib.util.spec_from_file_location(
    "hackylens_hkflash", ROOT / "tools" / "hkflash.py"
)
assert HKFLASH_SPEC is not None and HKFLASH_SPEC.loader is not None
HKFLASH = importlib.util.module_from_spec(HKFLASH_SPEC)
HKFLASH_SPEC.loader.exec_module(HKFLASH)
SYMBOL_SPEC = importlib.util.spec_from_file_location(
    "hackylens_check_firmware_symbols",
    ROOT / "tools" / "check_firmware_symbols.py",
)
assert SYMBOL_SPEC is not None and SYMBOL_SPEC.loader is not None
CHECK_SYMBOLS = importlib.util.module_from_spec(SYMBOL_SPEC)
SYMBOL_SPEC.loader.exec_module(CHECK_SYMBOLS)


class BuildContractsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.board = BUILD_FIRMWARE.load_board("huskylens-sen0305")

    def test_generated_config_uses_canonical_version(self):
        expected = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
        with tempfile.TemporaryDirectory() as directory:
            stage = Path(directory)
            BUILD_FIRMWARE.write_config(stage, set())
            config = (stage / "hk_config.h").read_text(encoding="utf-8")
        self.assertIn(f'#define HACKYLENS_VERSION "{expected}"', config)
        self.assertIn("#define HK_MICROPYTHON_WDT_FAULT_INJECTION 0", config)

    def test_public_capability_headers_are_staged_and_included(self):
        with tempfile.TemporaryDirectory() as directory:
            stage = Path(directory) / "stage"
            stage.mkdir()
            BUILD_FIRMWARE.copy_tree_files(
                ROOT / "firmware" / "include",
                stage / "firmware" / "include",
            )
            BUILD_FIRMWARE.write_project_cmake(stage, None, self.board)
            project = (stage / "project.cmake").read_text(encoding="utf-8")
            common = (
                stage / "firmware" / "include" / "hackylens" /
                "capability" / "common.h"
            )
            self.assertTrue(common.is_file())
            self.assertIn("firmware/include", project.replace("\\", "/"))

    def test_private_owner_binding_wraps_existing_menu_lifecycle(self):
        app_header = (ROOT / "firmware" / "src" / "core" / "hk_app.h").read_text(
            encoding="utf-8"
        )
        menu = (ROOT / "firmware" / "src" / "core" / "hk_menu.c").read_text(
            encoding="utf-8"
        )
        startup = (
            ROOT / "firmware" / "src" / "runtime" / "firmware_startup.c"
        ).read_text(encoding="utf-8")
        runtime = (
            ROOT / "firmware" / "src" / "runtime" /
            "capability_owner_runtime.c"
        ).read_text(encoding="utf-8")
        self.assertNotIn("hk_owner_t", app_header)
        self.assertIn("s_owner_hooks.exit(app)", menu)
        self.assertIn("s_owner_hooks.enter(app)", menu)
        self.assertIn("menu_owner_hooks_set(&owner_hooks)", startup)
        self.assertIn(
            "hk_generated_capability_inventory_get(",
            runtime,
        )
        self.assertIn("hk_generated_capability_grants_for(app->id", runtime)

    def test_wdt_fault_injection_is_explicit_and_test_build_only(self):
        expected = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
        with tempfile.TemporaryDirectory() as directory:
            stage = Path(directory)
            BUILD_FIRMWARE.write_config(stage, set(), True)
            config = (stage / "hk_config.h").read_text(encoding="utf-8")
        self.assertIn("#define HK_MICROPYTHON_WDT_FAULT_INJECTION 1", config)
        self.assertIn(f'#define HACKYLENS_VERSION "{expected}-wdtfi"', config)

    def test_release_partition_check_does_not_claim_build_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / "firmware.bin"
            image.write_bytes(b"arbitrary small bytes")
            PACKAGE_RELEASE.validate_release_firmware(image, self.board)
            source = (ROOT / "tools" / "package_release.py").read_text(
                encoding="utf-8"
            )
            self.assertIn("read_and_validate_attestation", source)
            self.assertNotIn("version.encode", source)

    def test_build_package_and_flasher_share_canonical_partition(self):
        flash, partitions = PACKAGE_RELEASE.load_validated(
            self.board.flash_layout_path
        )
        firmware = PACKAGE_RELEASE.partition_by_name(partitions, "firmware")
        expected_limit = firmware["offset"] + firmware["size"]
        self.assertEqual(
            HKFLASH.firmware_erase_length(
                1,
                flash_address=firmware["offset"],
                firmware_flash_limit=expected_limit,
                flash_erase_size=flash["erase_size"],
            ),
            flash["erase_size"],
        )

    def test_flasher_rejects_payload_past_firmware_partition(self):
        flash, partitions = PACKAGE_RELEASE.load_validated(
            self.board.flash_layout_path
        )
        firmware = PACKAGE_RELEASE.partition_by_name(partitions, "firmware")
        address = firmware["offset"]
        limit = address + firmware["size"]
        call = lambda size: HKFLASH.firmware_erase_length(
            size,
            flash_address=address,
            firmware_flash_limit=limit,
            flash_erase_size=flash["erase_size"],
        )
        self.assertEqual(call(firmware["size"]), firmware["size"])
        with self.assertRaisesRegex(ValueError, "canonical partition"):
            call(firmware["size"] + 1)
        with self.assertRaisesRegex(ValueError, "empty"):
            call(0)

    def test_release_output_rejects_stray_fault_image(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            (output / "hackylens-wdtfi.bin").write_bytes(b"danger")
            with self.assertRaisesRegex(SystemExit, "unexpected files"):
                PACKAGE_RELEASE.ensure_allowlisted_output(
                    output, {"hackylens-v0.2.0.bin"}
                )

    def test_release_output_rejects_symlink_escape(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "release"
            output.mkdir()
            external = root / "external.bin"
            external.write_bytes(b"must remain unchanged")
            linked = output / "hackylens.bin"
            try:
                linked.symlink_to(external)
            except (OSError, NotImplementedError) as exc:
                linked.write_bytes(b"placeholder")
                with mock.patch.object(Path, "is_symlink", return_value=True):
                    with self.assertRaisesRegex(
                        SystemExit, "must not be a symlink"
                    ):
                        PACKAGE_RELEASE.validate_output_paths(output, [linked])
            else:
                with self.assertRaisesRegex(SystemExit, "must not be a symlink"):
                    PACKAGE_RELEASE.validate_output_paths(output, [linked])
            self.assertEqual(external.read_bytes(), b"must remain unchanged")

    def test_generated_dependency_manifest_lists_actual_embed_sources(self):
        with tempfile.TemporaryDirectory() as directory:
            embed = Path(directory)
            (embed / "py").mkdir()
            (embed / "port").mkdir()
            (embed / "py" / "runtime.c").write_text("", encoding="utf-8")
            (embed / "port" / "embed_util.c").write_text("", encoding="utf-8")
            (embed / "port" / "mphalport.c").write_text("", encoding="utf-8")
            manifest = PACKAGE_RELEASE.firmware_dependency_manifest(
                "0.2.0", embed
            )
        dependencies = manifest["dependencies"]
        self.assertEqual(dependencies[0]["revision"], PACKAGE_RELEASE.MICROPYTHON_REVISION)
        self.assertEqual(
            dependencies[0]["included_source_files"],
            ["port/embed_util.c", "py/runtime.c"],
        )
        self.assertEqual(dependencies[1]["revision"], PACKAGE_RELEASE.LITTLEFS_REVISION)

    def test_firmware_symbol_feature_gate(self):
        symbols = {
            "mp_embed_init",
            "mp_builtin_max_obj",
            "mp_builtin_min_obj",
            "mp_builtin_sum_obj",
            "micropython_runtime_start",
            "hmpy_session_begin",
            "userfs_mount",
        }
        groups = CHECK_SYMBOLS.verify(symbols, "present")
        self.assertTrue(all(groups.values()))
        CHECK_SYMBOLS.verify({"main", "camera_tick"}, "absent")
        with self.assertRaisesRegex(ValueError, "leaked symbols"):
            CHECK_SYMBOLS.verify(symbols, "absent")
        with self.assertRaisesRegex(ValueError, "missing symbol groups"):
            CHECK_SYMBOLS.verify({"mp_embed_init"}, "present")
        with self.assertRaisesRegex(ValueError, "missing required symbols"):
            CHECK_SYMBOLS.verify(
                {
                    "mp_embed_init",
                    "micropython_runtime_start",
                    "hmpy_session_begin",
                    "userfs_mount",
                },
                "present",
            )

    def test_invalid_version_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            version_file = Path(directory) / "VERSION"
            version_file.write_text('0.2.0"\\n#define BAD 1', encoding="utf-8")
            with self.assertRaises(RuntimeError):
                BUILD_FIRMWARE.read_firmware_version(version_file)

    def test_native_iterator_patch_applies_to_pinned_runtime(self):
        source = ROOT / "_deps" / "micropython" / "py" / "runtime.c"
        if not source.is_file():
            self.skipTest("pinned MicroPython checkout is not bootstrapped")
        if shutil.which("git") is None:
            self.skipTest("git is unavailable")
        (ROOT / "build").mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as directory:
            package = Path(directory)
            (package / "py").mkdir()
            shutil.copy2(source, package / "py" / "runtime.c")
            BUILD_FIRMWARE.apply_micropython_native_poll_patch(package)
            patched = (package / "py" / "runtime.c").read_text(encoding="utf-8")
        self.assertEqual(patched.count("MICROPY_PORT_ITERNEXT_HOOK"), 2)
        self.assertIn(
            "mp_obj_t mp_iternext_allow_raise(mp_obj_t o_in) {\n"
            "    MICROPY_PORT_ITERNEXT_HOOK",
            patched,
        )
        self.assertIn(
            "mp_obj_t mp_iternext(mp_obj_t o_in) {\n"
            "    MICROPY_PORT_ITERNEXT_HOOK",
            patched,
        )

    def test_watchdog_reset_cause_guards_startup(self):
        watchdog = (ROOT / "platforms" / "k210" / "hal" / "hal_watchdog.c").read_text(
            encoding="utf-8"
        )
        autostart = (
            ROOT / "firmware" / "src" / "controllers" / "autostart_controller.c"
        ).read_text(encoding="utf-8")
        app = (
            ROOT / "firmware" / "src" / "apps" / "micropython" /
            "micropython_app.c"
        ).read_text(encoding="utf-8")
        self.assertNotIn("SYSCTL_RESET_SOC", watchdog)
        self.assertIn("SYSCTL_RESET_STATUS_WDT1", watchdog)
        self.assertIn("WDT_DEVICE_1", watchdog)
        self.assertIn("hal_watchdog_force_reset", watchdog)
        self.assertNotIn("wdt_feed", watchdog)
        self.assertNotIn("wdt_clear_interrupt", watchdog)
        self.assertIn("hal_watchdog_reset_detected()", autostart)
        self.assertIn("boot_internal_watchdog_reset_detected()", app)
        enter = app.split("void micropython_enter", 1)[1].split(
            "void micropython_exit", 1
        )[0]
        self.assertNotIn("micropython_program_run_file", enter)
        self.assertNotIn("micropython_runtime_start", enter)
        self.assertIn("WDT RECOVERY: PREVIOUS RUN RESET", enter)

    def test_micropython_terminal_handoff_is_ordered_and_preserves_ticket(self):
        runtime = (
            ROOT / "firmware" / "src" / "services" /
            "micropython_runtime.c"
        ).read_text(encoding="utf-8")
        executor = (
            ROOT / "firmware" / "src" / "services" /
            "core1_executor.c"
        ).read_text(encoding="utf-8")

        complete = executor.split(
            "uint8_t core1_executor_complete(uint32_t ticket)", 1
        )[1].split("uint8_t core1_executor_wait", 1)[0]
        load = complete.index("completed = control->complete_ticket;")
        self.assertGreater(
            complete.index("__sync_synchronize();", load),
            load,
            "completion observation must acquire worker publications",
        )

        worker = runtime.split(
            "static void micropython_worker(void *context)", 1
        )[1].split("uint8_t micropython_runtime_start", 1)[0]
        terminal = worker.index(
            "if(shared->exit_reason == MICROPYTHON_EXIT_COMPLETE)"
        )
        self.assertLess(worker.index("mp_embed_deinit();"), terminal)
        self.assertLess(terminal, worker.rindex("__sync_synchronize();"))

        start = runtime.split(
            "uint8_t micropython_runtime_start", 1
        )[1].split("uint8_t micropython_runtime_request_stop", 1)[0]
        self.assertLess(start.index("if(g_ticket)"), start.index("micropython_capability_bridge_prepare"))

        poll = runtime.split(
            "void micropython_runtime_poll(void)", 1
        )[1].split("void micropython_runtime_get_status", 1)[0]
        self.assertIn("core1_executor_complete(g_ticket) ||", poll)
        self.assertIn("!micropython_state_active(shared->state)", poll)
        self.assertLess(poll.index("g_ticket = 0U;"), poll.index("micropython_capability_bridge_cleanup();"))

    def test_micropython_build_date_is_pinned_to_dependency_revision(self):
        with mock.patch.dict(
            "os.environ", {"SOURCE_DATE_EPOCH": "1"}, clear=False
        ):
            environment = BUILD_FIRMWARE.micropython_build_environment()

        epoch = BUILD_FIRMWARE.MICROPYTHON_SOURCE_DATE_EPOCH
        self.assertEqual(environment["SOURCE_DATE_EPOCH"], epoch)
        self.assertNotEqual(epoch, "1")
        self.assertEqual(
            datetime.datetime.fromtimestamp(
                int(epoch), datetime.timezone.utc
            ).date().isoformat(),
            "2026-04-06",
        )


if __name__ == "__main__":
    unittest.main()
