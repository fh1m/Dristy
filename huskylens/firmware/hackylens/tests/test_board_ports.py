from __future__ import annotations

import argparse
import contextlib
import copy
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import board_contract
import build_firmware
import check_arch
import check_phase1_resources
import firmware_attestation
import firmware_sidecar
import gen_board
import gen_flash_layout
import hkflash
import make_image
import package_release


class BoardDescriptorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.registry = board_contract.load_registry()
        cls.runtime = board_contract.load_board("huskylens-sen0305")
        cls.cube = board_contract.load_board("sipeed-maix-cube")

    def validate(self, data: dict) -> None:
        board_contract.validate_board_data(data, self.registry)

    def test_runtime_and_conformance_invariants(self) -> None:
        self.assertEqual(self.runtime.runtime_profile, "hackylens-full")
        self.assertTrue(self.runtime.releaseable)
        self.assertIsNone(self.cube.runtime_profile)
        self.assertFalse(self.cube.releaseable)
        self.assertEqual(
            set(self.registry.data["runtime_base_devices"]),
            {
                "processor", "internal-flash", "display", "buttons", "lights",
                "sd-card", "external-uart", "external-i2c",
            },
        )

        missing = copy.deepcopy(self.runtime.data)
        del missing["runtime_profile"]
        with self.assertRaisesRegex(board_contract.ContractError, "runtime_profile"):
            self.validate(missing)

        conformance_profile = copy.deepcopy(self.cube.data)
        conformance_profile["runtime_profile"] = "hackylens-full"
        with self.assertRaisesRegex(board_contract.ContractError, "forbidden"):
            self.validate(conformance_profile)

        releaseable = copy.deepcopy(self.cube.data)
        releaseable["releaseable"] = True
        with self.assertRaisesRegex(board_contract.ContractError, "support=runtime"):
            self.validate(releaseable)

    def test_profile_requires_present_driver_binding(self) -> None:
        data = copy.deepcopy(self.runtime.data)
        camera = next(item for item in data["devices"] if item["kind"] == "camera")
        camera["support"] = "known-unsupported"
        del camera["driver"]
        with self.assertRaisesRegex(board_contract.ContractError, "missing driver-supported"):
            self.validate(data)

    def test_unknown_schema_fields_and_ids_are_rejected(self) -> None:
        unknown_field = copy.deepcopy(self.runtime.data)
        unknown_field["capabilities"] = []
        with self.assertRaisesRegex(board_contract.ContractError, "unknown field"):
            self.validate(unknown_field)

        unknown_device = copy.deepcopy(self.runtime.data)
        unknown_device["devices"][0]["id"] = "invented-k210"
        with self.assertRaisesRegex(board_contract.ContractError, "unknown device ID"):
            self.validate(unknown_device)

        wrong_kind = copy.deepcopy(self.runtime.data)
        wrong_kind["devices"][0]["kind"] = "display"
        with self.assertRaisesRegex(board_contract.ContractError, "requires 'processor'"):
            self.validate(wrong_kind)

        wrong_driver = copy.deepcopy(self.runtime.data)
        wrong_driver["devices"][0]["driver"] = "lcd-st7789"
        with self.assertRaisesRegex(board_contract.ContractError, "not allowed"):
            self.validate(wrong_driver)

    def test_registry_function_mapping_and_defaults_are_cross_checked(self) -> None:
        self.assertEqual(set(board_contract.DEFAULT_RANGES), board_contract.DEFAULT_FIELDS)
        runtime_flash = next(
            device for device in self.runtime.data["devices"]
            if device["kind"] == "internal-flash"
        )
        cube_flash = next(
            device for device in self.cube.data["devices"]
            if device["kind"] == "internal-flash"
        )
        self.assertEqual(runtime_flash["id"], "board-internal-flash")
        self.assertEqual(cube_flash["id"], "gd25lq128")

        wrong_peripheral = copy.deepcopy(self.runtime.data)
        wrong_peripheral["routes"][0]["peripheral"] = "timer2"
        with self.assertRaisesRegex(board_contract.ContractError, "requires 'timer0'"):
            self.validate(wrong_peripheral)

        gpiohs = copy.deepcopy(self.runtime.data)
        gpiohs["defaults"]["gpiohs_lcd_reset"] = 13
        with self.assertRaisesRegex(board_contract.ContractError, "must match"):
            self.validate(gpiohs)

        spi = copy.deepcopy(self.runtime.data)
        spi["defaults"]["lcd_spi"] = 1
        with self.assertRaisesRegex(board_contract.ContractError, "must match"):
            self.validate(spi)

        chip_select = copy.deepcopy(self.runtime.data)
        chip_select["defaults"]["lcd_chip_select"] = 2
        with self.assertRaisesRegex(board_contract.ContractError, "must match"):
            self.validate(chip_select)

        invalid_defaults = {
            "flash_spi": 2,
            "flash_chip_select": 1,
            "sd_chip_select": 4,
            "led_pwm_device": 3,
            "rgb_pwm_channel1": 4,
            "camera_sccb_address": 0x80,
            "lcd_spi_hz": 0,
            "camera_sccb_hz": 0,
            "camera_xclk_hz": 0xFFFFFFFF,
            "camera_max_width": 641,
        }
        for field, value in invalid_defaults.items():
            with self.subTest(field=field):
                data = copy.deepcopy(self.runtime.data)
                data["defaults"][field] = value
                with self.assertRaises(board_contract.ContractError):
                    self.validate(data)

        pwm_relation = copy.deepcopy(self.runtime.data)
        pwm_relation["defaults"]["led_pwm_device"] = 1
        with self.assertRaisesRegex(board_contract.ContractError, "must match"):
            self.validate(pwm_relation)

    def test_registry_route_roles_prevent_logical_signal_swaps(self) -> None:
        all_route_macros = {
            route["macro"]
            for board in (self.runtime, self.cube)
            for route in board.data["routes"]
        }
        self.assertTrue(all_route_macros.issubset(self.registry.data["route_roles"]))

        spi_swap = copy.deepcopy(self.runtime.data)
        lcd_mosi = next(
            route for route in spi_swap["routes"] if route["macro"] == "IO_LCD_MOSI"
        )
        sd_data0 = next(
            route for route in spi_swap["routes"] if route["macro"] == "IO_SD_D0"
        )
        lcd_mosi["function"], sd_data0["function"] = (
            sd_data0["function"], lcd_mosi["function"]
        )
        lcd_mosi["peripheral"], sd_data0["peripheral"] = (
            sd_data0["peripheral"], lcd_mosi["peripheral"]
        )
        with self.assertRaisesRegex(board_contract.ContractError, "route role"):
            self.validate(spi_swap)

        camera_swap = copy.deepcopy(self.runtime.data)
        sccb_clock = next(
            route for route in camera_swap["routes"]
            if route["macro"] == "IO_CAM_SCCB_SCLK"
        )
        sccb_data = next(
            route for route in camera_swap["routes"]
            if route["macro"] == "IO_CAM_SCCB_SDA"
        )
        sccb_clock["function"], sccb_data["function"] = (
            sccb_data["function"], sccb_clock["function"]
        )
        with self.assertRaisesRegex(board_contract.ContractError, "route role"):
            self.validate(camera_swap)

    def test_registry_tables_reject_unknown_peripherals_and_duplicate_macros(self) -> None:
        registry_text = board_contract.REGISTRY_PATH.read_text(encoding="utf-8")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "devices.toml"
            path.write_text(
                registry_text.replace(
                    'peripheral = "uart1"\nmacro = "FUNC_UART1_RX"',
                    'peripheral = "invented-uart"\nmacro = "FUNC_UART1_RX"',
                    1,
                ),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(board_contract.ContractError, "unknown instance"):
                board_contract.load_registry(path)

            path.write_text(
                registry_text.replace("FUNC_UART1_RX", "FUNC_UART1_TX", 1),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(board_contract.ContractError, "duplicate"):
                board_contract.load_registry(path)

            path.write_text(
                registry_text
                + "\n[runtime_profiles.incomplete-future]\n"
                + 'required_devices = ["processor"]\n',
                encoding="utf-8",
            )
            with self.assertRaisesRegex(board_contract.ContractError, "runtime base"):
                board_contract.load_registry(path)

            mux_route = (
                '  { macro = "IO_EXTERNAL_I2C_T", function = "i2c0-sda", '
                'pin = 35, peripheral = "i2c0" },\n'
            )
            path.write_text(registry_text.replace(mux_route, "", 1), encoding="utf-8")
            with self.assertRaisesRegex(board_contract.ContractError, "exactly four"):
                board_contract.load_registry(path)

            path.write_text(
                registry_text.replace(
                    'macro = "IO_EXTERNAL_UART_T", function = "uart1-tx", pin = 35',
                    'macro = "IO_EXTERNAL_UART_T", function = "uart1-tx", pin = 36',
                    1,
                ),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(board_contract.ContractError, "two symmetric modes"):
                board_contract.load_registry(path)

    def test_programming_metadata_is_strict(self) -> None:
        reset = copy.deepcopy(self.runtime.data)
        reset["programming"]["reset_profile"] = "board-specific-special-case"
        with self.assertRaisesRegex(board_contract.ContractError, "unknown profile"):
            self.validate(reset)

        vid = copy.deepcopy(self.runtime.data)
        vid["programming"]["usb_detection"]["vids"] = ["1a86"]
        with self.assertRaisesRegex(board_contract.ContractError, "uppercase"):
            self.validate(vid)

        empty_vids = copy.deepcopy(self.runtime.data)
        empty_vids["programming"]["usb_detection"]["vids"] = []
        with self.assertRaisesRegex(board_contract.ContractError, "at least one VID"):
            self.validate(empty_vids)

        unsupported = copy.deepcopy(self.cube.data)
        unsupported["programming"]["flash_baud"] = 2_000_000
        with self.assertRaisesRegex(board_contract.ContractError, "forbids hardware defaults"):
            self.validate(unsupported)

    def test_route_pin_and_function_collisions_are_rejected(self) -> None:
        pin = copy.deepcopy(self.runtime.data)
        pin["routes"][1]["pin"] = pin["routes"][0]["pin"]
        with self.assertRaisesRegex(board_contract.ContractError, "duplicate pin"):
            self.validate(pin)

        function = copy.deepcopy(self.runtime.data)
        function["routes"][1]["function"] = function["routes"][0]["function"]
        function["routes"][1]["peripheral"] = function["routes"][0]["peripheral"]
        with self.assertRaisesRegex(board_contract.ContractError, "route role|duplicate function"):
            self.validate(function)

    def test_multiple_functions_of_one_peripheral_are_valid(self) -> None:
        self.validate(copy.deepcopy(self.runtime.data))
        spi0 = [
            route for route in self.runtime.data["routes"]
            if route["peripheral"] == "spi0"
        ]
        self.assertGreaterEqual(len({route["function"] for route in spi0}), 3)

    def test_legacy_external_runtime_mux_is_explicit_and_strict(self) -> None:
        routes = [
            route for route in self.runtime.data["routes"]
            if route.get("runtime_mux_group") == "external-four-pin-mode"
        ]
        self.assertEqual(len(routes), 4)
        self.assertEqual({route["pin"] for route in routes}, {34, 35})
        self.assertEqual({route["peripheral"] for route in routes}, {"uart1", "i2c0"})
        self.validate(copy.deepcopy(self.runtime.data))

        ungrouped = copy.deepcopy(self.runtime.data)
        del ungrouped["routes"][-1]["runtime_mux_group"]
        with self.assertRaisesRegex(board_contract.ContractError, "duplicate pin"):
            self.validate(ungrouped)

        conformance = copy.deepcopy(self.runtime.data)
        conformance["support"] = "conformance"
        conformance["releaseable"] = False
        del conformance["runtime_profile"]
        with self.assertRaisesRegex(board_contract.ContractError, "only runtime ports"):
            self.validate(conformance)

        renamed = copy.deepcopy(self.runtime.data)
        for route in renamed["routes"]:
            if route.get("runtime_mux_group") == "external-four-pin-mode":
                route["runtime_mux_group"] = "invented-runtime-mux"
        with self.assertRaisesRegex(board_contract.ContractError, "registry-defined"):
            self.validate(renamed)

        shortened = copy.deepcopy(self.runtime.data)
        shortened["routes"] = [
            route for route in shortened["routes"]
            if route["macro"] != "IO_EXTERNAL_I2C_T"
        ]
        shortened["connectors"][0]["routes"].remove("external-i2c-data")
        with self.assertRaisesRegex(board_contract.ContractError, "exactly match"):
            self.validate(shortened)

        moved = copy.deepcopy(self.runtime.data)
        next(
            route for route in moved["routes"]
            if route["macro"] == "IO_EXTERNAL_UART_R"
        )["pin"] = 33
        moved["connectors"][0]["pins"] = [33, 34, 35]
        with self.assertRaisesRegex(board_contract.ContractError, "exactly match"):
            self.validate(moved)

    def test_connector_pins_protocols_and_routes_are_cross_checked(self) -> None:
        wrong_pin = copy.deepcopy(self.runtime.data)
        wrong_pin["connectors"][0]["pins"] = [33, 35]
        with self.assertRaisesRegex(board_contract.ContractError, "exactly match"):
            self.validate(wrong_pin)

        wrong_protocol = copy.deepcopy(self.runtime.data)
        wrong_protocol["connectors"][0]["protocols"] = ["uart1"]
        with self.assertRaisesRegex(board_contract.ContractError, "route peripherals"):
            self.validate(wrong_protocol)

        unknown_route = copy.deepcopy(self.runtime.data)
        unknown_route["connectors"][0]["routes"][0] = "imaginary-route"
        with self.assertRaisesRegex(board_contract.ContractError, "unknown route"):
            self.validate(unknown_route)

    def test_cube_grove_records_only_source_verified_physical_pins(self) -> None:
        connector = next(
            item for item in self.cube.data["connectors"] if item["id"] == "grove"
        )
        self.assertEqual(connector["pins"], [24, 25])
        self.assertEqual(connector["protocols"], [])
        self.assertEqual(connector["routes"], [])
        kinds = {device["kind"] for device in self.cube.data["devices"]}
        self.assertNotIn("external-uart", kinds)
        self.assertNotIn("external-i2c", kinds)

        invented_protocol = copy.deepcopy(self.cube.data)
        invented_protocol["connectors"][0]["protocols"] = ["uart1"]
        with self.assertRaisesRegex(
            board_contract.ContractError, "protocol semantics require explicit routes"
        ):
            self.validate(invented_protocol)

        runtime_physical_only = copy.deepcopy(self.runtime.data)
        runtime_physical_only["connectors"][0] = {
            "id": "physical-only",
            "kind": "grove",
            "pins": [24, 25],
            "protocols": [],
            "routes": [],
        }
        with self.assertRaisesRegex(
            board_contract.ContractError, "physical-only connector inventory"
        ):
            self.validate(runtime_physical_only)

    def test_compile_time_exclusive_route_selection(self) -> None:
        data = copy.deepcopy(self.runtime.data)
        first = data["routes"][0]
        first["exclusive_group"] = "display-backlight-mux"
        alternate = copy.deepcopy(first)
        alternate["id"] = "lcd-backlight-alternate"
        data["routes"].append(alternate)
        data["route_selections"] = {
            "display-backlight-mux": first["id"],
        }
        self.validate(data)
        board = board_contract.Board(
            path=Path("board.toml"), data=data, registry=self.registry
        )
        selected_ids = {route["id"] for route in board.selected_routes()}
        self.assertIn(first["id"], selected_ids)
        self.assertNotIn(alternate["id"], selected_ids)

        del data["route_selections"]
        with self.assertRaisesRegex(board_contract.ContractError, "must select exactly one"):
            self.validate(data)

    def test_conformance_may_omit_only_noncompiled_route_group(self) -> None:
        self.validate(copy.deepcopy(self.cube.data))
        data = copy.deepcopy(self.cube.data)
        data["routes"][0]["exclusive_group"] = "unqualified-alternatives"
        data["routes"][1]["exclusive_group"] = "unqualified-alternatives"
        data["routes"][0]["compile"] = True
        with self.assertRaisesRegex(board_contract.ContractError, "every route compile=false"):
            self.validate(data)

    def test_known_unsupported_inventory_is_not_driver_supported(self) -> None:
        inventory = gen_board.render_inventory(self.cube)
        self.assertIn("#define HK_BOARD_HAS_DISPLAY 1U", inventory)
        self.assertIn("#define HK_BOARD_DRIVER_DISPLAY 0U", inventory)
        self.assertEqual(self.cube.driver_supported_kinds(), {"processor"})

    def test_every_board_has_current_generated_files_and_mandatory_early_init(self) -> None:
        for board in (self.runtime, self.cube):
            self.assertEqual(gen_board.generate(board, check=True), [])
            board_contract.validate_board_source(board)

        with tempfile.TemporaryDirectory() as directory:
            board_dir = Path(directory) / "missing-early"
            board_dir.mkdir()
            source = board_dir / "board.c"
            source.write_text(
                "const hk_board_ops_t hk_board_ops = { .early_init = NULL };\n",
                encoding="utf-8",
            )
            board = board_contract.Board(
                path=board_dir / "board.toml",
                data={
                    "id": "missing-early",
                    "devices": [],
                },
                registry=self.registry,
            )
            with self.assertRaisesRegex(board_contract.ContractError, "early_init"):
                board_contract.validate_board_source(board)

    def test_commented_out_board_ops_cannot_satisfy_source_validation(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            board_dir = Path(directory)
            (board_dir / "board.c").write_text(
                "/* const hk_board_ops_t hk_board_ops = { .early_init = fake }; */\n"
                "const hk_board_ops_t hk_board_ops = { .early_init = NULL };\n",
                encoding="utf-8",
            )
            board = board_contract.Board(
                path=board_dir / "board.toml",
                data={"id": "comment-test", "devices": []},
                registry=self.registry,
            )
            with self.assertRaisesRegex(board_contract.ContractError, "early_init"):
                board_contract.validate_board_source(board)

    def test_flash_layout_bytes_are_exactly_canonical(self) -> None:
        for board in (self.runtime, self.cube):
            parsed = gen_flash_layout.load_layout(board.flash_layout_path)
            self.assertEqual(
                board.flash_layout_path.read_bytes(),
                gen_flash_layout.canonical_json_bytes(parsed),
            )
        attributes = (ROOT / ".gitattributes").read_text(encoding="utf-8")
        self.assertIn("boards/*/flash_layout.json text eol=lf", attributes)

        with tempfile.TemporaryDirectory() as directory:
            bad = Path(directory) / "layout.json"
            parsed = gen_flash_layout.load_layout(self.cube.flash_layout_path)
            bad.write_text(json.dumps(parsed), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "not canonical"):
                gen_flash_layout.load_layout(bad)
            parsed["invented_contract_field"] = True
            bad.write_bytes(gen_flash_layout.canonical_json_bytes(parsed))
            with self.assertRaisesRegex(ValueError, "unknown field"):
                gen_flash_layout.load_layout(bad)

    def test_cube_layout_is_conservative_and_not_runtime_storage(self) -> None:
        _flash, partitions = gen_flash_layout.load_validated(
            self.cube.flash_layout_path
        )
        self.assertEqual(
            [(p["name"], p["offset"], p["size"], p["runtime_writable"])
             for p in partitions],
            [
                ("firmware", 0, 0x00800000, False),
                ("reserved", 0x00800000, 0x00800000, False),
            ],
        )


class BoardCompositionAndCliTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.runtime = board_contract.load_board("huskylens-sen0305")
        cls.cube = board_contract.load_board("sipeed-maix-cube")

    def run_tool(self, tool: str, *args: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(TOOLS / tool), *args],
            cwd=ROOT,
            text=True,
            capture_output=True,
            timeout=30,
        )

    def test_board_is_mandatory_and_unknown_board_is_rejected(self) -> None:
        missing = self.run_tool("build_firmware.py", "full")
        self.assertEqual(missing.returncode, 2)
        self.assertIn("--board", missing.stderr)

        invalid = self.run_tool(
            "build_firmware.py", "full", "--board", "unknown-k210-board"
        )
        self.assertEqual(invalid.returncode, 2)
        self.assertIn("unknown ID", invalid.stderr)

        checker = self.run_tool("check_board_ports.py")
        self.assertEqual(checker.returncode, 2)

    def test_cube_compile_conformance_succeeds(self) -> None:
        result = self.run_tool(
            "check_board_ports.py", "--board", "sipeed-maix-cube", "--compile"
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("passed for 1 board", result.stdout)
        harness = (TOOLS / "board_conformance_harness.c").read_text(encoding="utf-8")
        for header in ("pins.h", "defaults.h", "inventory.h", "flash_layout.h"):
            self.assertIn(f'#include "{header}"', harness)
        self.assertIn("&hk_board_ops", harness)

    def test_cube_full_package_image_and_flash_fail_early(self) -> None:
        full = self.run_tool(
            "build_firmware.py", "full", "--board", "sipeed-maix-cube"
        )
        self.assertEqual(full.returncode, 2)
        self.assertIn("conformance-only", full.stderr)

        package = self.run_tool(
            "package_release.py", "--board", "sipeed-maix-cube"
        )
        self.assertNotEqual(package.returncode, 0)
        self.assertIn("not releaseable", package.stderr)

        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / "cube.bin"
            image.write_bytes(b"cube")
            artifact = self.run_tool(
                "make_image.py", str(image), "--board", "sipeed-maix-cube"
            )
            self.assertEqual(artifact.returncode, 2)
            self.assertIn("conformance-only", artifact.stderr)
            flash = self.run_tool(
                "hkflash.py", "flash", str(image),
                "--board", "sipeed-maix-cube", "--port", "TEST",
            )
            self.assertNotEqual(flash.returncode, 0)
            self.assertIn("programming.supported=false", flash.stderr)

    def test_flash_defaults_come_from_programming_descriptor(self) -> None:
        args = hkflash.build_parser().parse_args(
            [
                "flash",
                "firmware.bin",
                "--board",
                "huskylens-sen0305",
            ]
        )
        hkflash.apply_board_configuration(args)

        self.assertEqual(args.reset_profile, "huskylens-uploader")
        self.assertIs(
            args.reset_connector,
            hkflash.connect_bootrom_uploader,
        )
        self.assertEqual(args.boot_baud, 115200)
        self.assertEqual(args.flash_baud, 2_000_000)
        self.assertEqual(args.flash_type, 1)
        self.assertEqual(args.io_mode, "qio")
        self.assertEqual(args.reset_attempts, 15)
        self.assertEqual(args.reboot_profile, "uploader-normal")
        self.assertIs(args.runtime_reboot_handler, hkflash.reset_to_boot_uploader)
        self.assertIs(args.reboot_fallback_handler, hkflash.reset_to_boot_uploader)

    def test_named_programming_profiles_dispatch_without_board_id_behavior(self) -> None:
        self.assertEqual(
            set(hkflash.RESET_PROFILE_CONNECTORS),
            set(self.runtime.registry.data["reset_profiles"]) - {"manual"},
        )
        self.assertEqual(
            set(hkflash.REBOOT_PROFILE_HANDLERS),
            set(self.runtime.registry.data["reboot_profiles"]),
        )
        calls: list[object] = []
        args = SimpleNamespace(
            runtime_reboot_handler=lambda serial: calls.append(serial),
            reboot_profile="test-profile",
        )
        serial = object()
        hkflash.apply_runtime_reboot_profile(args, serial)
        self.assertEqual(calls, [serial])

        parser_args = hkflash.build_parser().parse_args([
            "flash", "firmware.bin", "--board", self.runtime.id,
            "--uploader-reset",
        ])
        hkflash.apply_board_configuration(parser_args)
        self.assertEqual(
            parser_args.reset_profile,
            self.runtime.programming["reset_profile"],
        )

    def test_staging_contains_exactly_one_selected_bsp(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            sdk = Path(directory)
            with contextlib.redirect_stdout(io.StringIO()):
                stage = build_firmware.stage_target(
                    sdk,
                    "full",
                    self.runtime,
                    set(build_firmware.APP_MODULES),
                )
            boards = sorted(
                path.parent.name
                for path in (stage / "boards").glob("*/board.c")
            )
            self.assertEqual(boards, ["huskylens-sen0305"])
            self.assertFalse((stage / "boards" / "sipeed-maix-cube").exists())

    def test_build_time_requirements_exclude_or_fail_required_app(self) -> None:
        disabled, exclusions = build_firmware.compose_apps(
            self.cube, set(), set()
        )
        self.assertEqual(disabled, set(build_firmware.APP_MODULES))
        self.assertTrue(exclusions)
        self.assertTrue(all(item["code"] == "driver-unsupported"
                            for item in exclusions))
        with self.assertRaisesRegex(RuntimeError, "required app"):
            build_firmware.compose_apps(self.cube, set(), {"camera"})

        source_text = "\n".join(
            path.read_text(encoding="utf-8")
            for path in (ROOT / "firmware" / "src").rglob("*.[ch]")
        )
        self.assertNotIn("app_requirements.toml", source_text)

        requirements = build_firmware.load_app_requirements()
        self.assertNotIn("lights", requirements["micropython"])
        capability_requirements = (
            build_firmware.capability_inventory.load_app_requirements()
        )
        self.assertTrue(any(
            request.id == "hackylens.cap.lights"
            for request in capability_requirements["micropython"].required
        ))
        binding_service = (
            ROOT
            / "firmware"
            / "src"
            / "adapters"
            / "micropython"
            / "micropython_capability_bridge.c"
        ).read_text(encoding="utf-8")
        for token in ("hk_lights_set_level", "hk_lights_set_rgb"):
            self.assertIn(token, binding_service)
        for token in ("lights_illum_set", "lights_rgb_set", "hk_lights.h"):
            self.assertNotIn(token, binding_service)

    def test_app_binding_still_forbids_board_hal_and_sdk(self) -> None:
        app = "apps/buttons/buttons_view.c"
        self.assertIsNone(
            check_arch.layer_violation(
                app, "../../ui/display_binding.h", "ui/display_binding.h"
            )
        )
        self.assertIsNotNone(
            check_arch.layer_violation(app, "hal_time.h", None)
        )
        self.assertIsNotNone(
            check_arch.layer_violation(app, "../../boards/example/board.h", None)
        )
        self.assertIsNotNone(check_arch.layer_violation(app, "fpioa.h", None))
        self.assertIsNotNone(check_arch.layer_violation(app, "i2c.h", None))
        self.assertIsNotNone(check_arch.layer_violation(app, "plic.h", None))
        self.assertIsNotNone(
            check_arch.layer_violation(app, "sdk/drivers/include/i2c.h", None)
        )
        self.assertIsNotNone(
            check_arch.layer_violation(
                app, r"..\..\boards\example\board.h", None
            )
        )
        self.assertIsNotNone(
            check_arch.layer_violation(
                app, r"..\..\platforms\k210\hal\hal_time.h", None
            )
        )
        self.assertIsNotNone(
            check_arch.layer_violation(
                app, r"sdk\drivers\include\fpioa.h", None
            )
        )
        for sdk_header in (
            "nncase.h", "nncase/runtime/k210/runtime_module.h", "syslog.h"
        ):
            with self.subTest(sdk_header=sdk_header):
                self.assertIsNotNone(
                    check_arch.layer_violation(app, sdk_header, None)
                )
        for private_header in (
            "pins.h", "defaults.h", "inventory.h", "flash_layout.h",
            "hk_board_port.h",
        ):
            with self.subTest(private_header=private_header):
                self.assertIsNotNone(
                    check_arch.layer_violation(app, private_header, None)
                )
        self.assertIsNone(check_arch.layer_violation(app, "stdint.h", None))
        self.assertIsNone(
            check_arch.layer_violation(app, "i2c.h", "apps/buttons/i2c.h")
        )
        self.assertIsNotNone(
            check_arch.layer_violation(
                app, "../../internal/time_internal.h", "internal/time_internal.h"
            )
        )
        macro_include = (
            "#define K210_HEADER <fpioa.h>\n"
            "#include K210_HEADER\n"
        )
        self.assertEqual(check_arch.nonliteral_include_lines(macro_include), [2])
        self.assertIsNotNone(
            check_arch.layer_violation(
                app, "../../drivers/hk_lcd.h", "drivers/hk_lcd.h"
            ),
        )
        adversarial_includes = {
            "comment-split": "#/**/include <fpioa.h>\n",
            "backslash-spliced": "#inc\\\nlude <fpioa.h>\n",
            "digraph": "%:include <fpioa.h>\n",
            "trigraph": "??=include <fpioa.h>\n",
            "include-next": "#include_next <fpioa.h>\n",
            "import": "#import <fpioa.h>\n",
            "macro": "#define K210_HEADER <fpioa.h>\n#include K210_HEADER\n",
        }
        for label, source in adversarial_includes.items():
            with self.subTest(label=label):
                self.assertTrue(check_arch.nonliteral_include_lines(source))
        self.assertEqual(
            check_arch.sdk_token_lines(
                "void configure(void) { fpio\\\n"
                "a_set_function(1, 2); }\n"
            ),
            [(1, "fpioa_")],
        )
        self.assertEqual(
            check_arch.sdk_token_lines(
                "void configure(void) { fpio??/\n"
                "a_set_function(1, 2); }\n"
            ),
            [(1, "fpioa_")],
        )
        self.assertEqual(
            check_arch.sdk_token_lines(
                'const char *message = "fpioa_set_function";\n'
                "// fpioa_set_function(1, 2);\n"
            ),
            [],
        )
        self.assertEqual(
            check_arch.sdk_token_lines(
                "#define ROUTE(pin, function) fpioa_set_function(pin, function)\n"
            ),
            [(1, "fpioa_")],
        )

    def test_diagnostics_use_descriptor_generated_labels(self) -> None:
        diagnostics = {
            "boot": ROOT / "firmware" / "src" / "controllers" / "boot_controller.c",
            "terminal": (
                ROOT / "firmware" / "src" / "apps" / "terminal" /
                "terminal_controller.c"
            ),
            "external": (
                ROOT / "firmware" / "src" / "services" /
                "external_link_service.c"
            ),
            "external-header": (
                ROOT / "firmware" / "src" / "services" /
                "external_link_service.h"
            ),
        }
        texts = {
            name: path.read_text(encoding="utf-8")
            for name, path in diagnostics.items()
        }
        self.assertNotRegex(texts["boot"], re.compile(r"IO(?:18|19|22|23|26|27|28|29|30|31|32)\b"))
        self.assertNotIn("320x240", texts["terminal"])
        self.assertNotRegex(
            texts["external"] + texts["external-header"],
            re.compile(r"IO(?:34|35)\b"),
        )
        self.assertIn("IO_LCD_DC_OR_AUX_LABEL", texts["boot"])
        self.assertIn("HK_DISPLAY_REQUIRED_WIDTH", texts["terminal"])
        self.assertIn("IO_EXTERNAL_UART_R_LABEL", texts["external"])
        camera = (
            ROOT / "firmware" / "src" / "services" / "camera_session.c"
        ).read_text(encoding="utf-8")
        for macro in (
            "IO_CAM_PCLK_LABEL", "IO_CAM_XCLK_LABEL", "IO_CAM_HREF_LABEL",
            "IO_CAM_PWDN_LABEL", "IO_CAM_VSYNC_LABEL", "IO_CAM_RST_LABEL",
            "IO_CAM_SCCB_SCLK_LABEL", "IO_CAM_SCCB_SDA_LABEL",
        ):
            self.assertIn(macro, camera)
        self.assertNotIn("PCLK=47", camera)
        self.assertIn("IO_EXTERNAL_I2C_R_LABEL", texts["external"])
        self.assertIn("g_transport == EXTERNAL_LINK_I2C", texts["external"])

    def test_no_board_id_behavior_conditionals_exist(self) -> None:
        for path in TOOLS.glob("*.py"):
            text = path.read_text(encoding="utf-8")
            self.assertEqual(
                check_arch.board_behavior_violations(text), [], path.name
            )
        adversarial = {
            "multiline-if": (
                "if (\n  args.board\n  == 'huskylens-sen0305'\n):\n"
                "  reset_special()\n"
            ),
            "membership": (
                "if board.id in BOARD_HANDLERS:\n  BOARD_HANDLERS[board.id]()\n"
            ),
            "dynamic-identity-compare": (
                "if args.board == selected_board:\n  reset_special()\n"
            ),
            "match-case": (
                "match args.board:\n"
                "  case 'huskylens-sen0305': reset_special()\n"
            ),
            "behavior-table": (
                "RESET_HANDLERS = {\n"
                "  'huskylens-sen0305': reset_special,\n"
                "  'sipeed-maix-cube': reset_manual,\n"
                "}\n"
            ),
        }
        for label, source in adversarial.items():
            with self.subTest(label=label):
                self.assertTrue(
                    check_arch.board_behavior_violations(source)
                )
        self.assertEqual(
            check_arch.board_behavior_violations(
                "metadata = {'board_id': board.id}\n"
                "EXPECTED_BOARD = 'huskylens-sen0305'\n"
            ),
            [],
            "unconditional identity/provenance literals remain permitted",
        )


class ArtifactAndFlashSafetyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.board = board_contract.load_board("huskylens-sen0305")
        cls.flash, cls.partitions = gen_flash_layout.load_validated(
            cls.board.flash_layout_path
        )
        cls.firmware = gen_flash_layout.partition_by_name(
            cls.partitions, "firmware"
        )

    def args(self, *, sidecar: str | None = None,
             allow_missing: bool = False) -> SimpleNamespace:
        return SimpleNamespace(
            sidecar=sidecar,
            allow_missing_sidecar=allow_missing,
            board=self.board.id,
            board_descriptor=self.board,
            flash_address=self.firmware["offset"],
        )

    def metadata(self, image: Path) -> dict[str, object]:
        return firmware_sidecar.build_document(image, self.board)

    def attest(
        self,
        image: Path,
        *,
        disabled_apps: set[str] | None = None,
        path: Path | None = None,
    ) -> Path:
        output = path or image.with_suffix(".attestation.json")
        firmware_attestation.write(
            output,
            image,
            self.board,
            target="full",
            disabled_apps=disabled_apps or set(),
            exclusions=[],
            capabilities_sha256="a" * 64,
        )
        return output

    def test_sidecar_success_missing_override_and_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / "firmware.bin"
            image.write_bytes(b"safe image")
            sidecar = image.with_suffix(".json")
            sidecar.write_text(
                json.dumps(self.metadata(image)), encoding="utf-8"
            )
            hkflash.validate_image_sidecar(image, self.args())

            sidecar.unlink()
            with self.assertRaisesRegex(ValueError, "sidecar is required"):
                hkflash.validate_image_sidecar(image, self.args())
            hkflash.validate_image_sidecar(
                image, self.args(allow_missing=True)
            )

            mismatch = self.metadata(image)
            mismatch["board_id"] = "sipeed-maix-cube"
            sidecar.write_text(json.dumps(mismatch), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "sidecar mismatch"):
                hkflash.validate_image_sidecar(
                    image, self.args(allow_missing=True)
                )

            # An explicit missing path is not a way to hide mismatched
            # same-stem evidence under the raw-image override.
            missing_explicit = Path(directory) / "does-not-exist.json"
            with self.assertRaisesRegex(ValueError, "sidecar mismatch"):
                hkflash.validate_image_sidecar(
                    image,
                    self.args(
                        sidecar=str(missing_explicit), allow_missing=True
                    ),
                )

            # The same rule applies even when the alternate sidecar is valid:
            # every existing selected/default candidate must agree.
            valid_explicit = Path(directory) / "explicit.json"
            valid_explicit.write_text(
                json.dumps(self.metadata(image)), encoding="utf-8"
            )
            with self.assertRaisesRegex(ValueError, "sidecar mismatch"):
                hkflash.validate_image_sidecar(
                    image, self.args(sidecar=str(valid_explicit))
                )

    def test_sidecar_schema_is_exact_typed_and_current(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / "firmware.bin"
            image.write_bytes(b"safe image")
            document = self.metadata(image)
            expected_fields = firmware_sidecar.REQUIRED_FIELDS | {"runtime_profile"}
            self.assertEqual(set(document), expected_fields)
            firmware_sidecar.validate_document(document, image, self.board)

            mutations = {
                "root": [],
                "missing": {key: value for key, value in document.items() if key != "size"},
                "schema-type": {**document, "schema": 1.0},
                "filename": {**document, "image": "other.bin"},
                "version": {**document, "firmware_version": "0.2.0"},
                "contract": {**document, "board_contract_version": "0.2.0"},
                "profile": {**document, "build_profile": "full"},
                "platform": {**document, "platform_id": "other-platform"},
                "size-type": {**document, "size": True},
                "hash-type": {**document, "sha256": "ABC"},
                "layout": {**document, "flash_layout_sha256": "0" * 64},
                "address": {**document, "flash_address": "0x00000010"},
                "unknown": {**document, "release": True},
            }
            for label, mutation in mutations.items():
                with self.subTest(label=label):
                    with self.assertRaises(firmware_sidecar.SidecarError):
                        firmware_sidecar.validate_document(mutation, image, self.board)

    def test_build_attestation_schema_is_exact_canonical_and_hash_bound(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / "firmware.bin"
            image.write_bytes(b"exact build output")
            path = self.attest(image)
            document = json.loads(path.read_text(encoding="utf-8"))
            self.assertEqual(set(document), firmware_attestation.ROOT_FIELDS)
            self.assertEqual(
                path.read_bytes(), firmware_attestation.canonical_json_bytes(document)
            )
            self.assertEqual(document["build_profile"], "hackylens-full")
            self.assertTrue(document["release_qualified"])
            self.assertEqual(
                set(document["composition"]["enabled_apps"]),
                firmware_attestation.FULL_APP_IDS,
            )
            mutation = copy.deepcopy(document)
            mutation["composition"]["enabled_apps"].append("invented")
            with self.assertRaises(firmware_attestation.AttestationError):
                firmware_attestation.validate_document(
                    mutation, image, self.board
                )
            image.write_bytes(b"changed build output")
            with self.assertRaisesRegex(
                firmware_attestation.AttestationError, "image.sha256"
            ):
                firmware_attestation.read_and_validate(
                    path, image, self.board
                )

    def test_oversized_image_is_never_allowed(self) -> None:
        limit = self.firmware["offset"] + self.firmware["size"]
        with self.assertRaisesRegex(ValueError, "canonical partition"):
            hkflash.firmware_erase_length(
                self.firmware["size"] + 1,
                flash_address=self.firmware["offset"],
                firmware_flash_limit=limit,
                flash_erase_size=self.flash["erase_size"],
            )

    def test_board_qualified_image_and_sidecar_identity(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            image = temporary / "input.bin"
            image.write_bytes(b"phase-one exact firmware bytes")
            self.attest(image)
            result = subprocess.run(
                [
                    sys.executable,
                    str(TOOLS / "make_image.py"),
                    str(image),
                    "--board", self.board.id,
                    "--out-dir", str(temporary / "dist"),
                ],
                cwd=ROOT,
                text=True,
                capture_output=True,
                timeout=30,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            output = temporary / "dist" / f"hackylens-full-{self.board.id}.bin"
            metadata = json.loads(output.with_suffix(".json").read_text(encoding="utf-8"))
            self.assertEqual(metadata["schema"], 1)
            self.assertEqual(metadata["firmware_version"], "0.4.0")
            self.assertEqual(metadata["board_id"], self.board.id)
            self.assertEqual(metadata["platform_id"], self.board.registry.platform)
            self.assertEqual(metadata["board_contract_version"], "0.1.0")
            self.assertEqual(metadata["build_profile"], "hackylens-full")
            self.assertEqual(metadata["image"], output.name)
            self.assertEqual(metadata["size"], output.stat().st_size)
            firmware_sidecar.validate_document(metadata, output, self.board)
            firmware_attestation.read_and_validate(
                output.with_suffix(".attestation.json"),
                output,
                self.board,
                expected_target="full",
                require_release_qualified=True,
            )

    def test_image_qualification_rejects_arbitrary_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / "arbitrary.bin"
            image.write_bytes(b"not a selected firmware build 0.3.0")
            result = subprocess.run(
                [
                    sys.executable, str(TOOLS / "make_image.py"), str(image),
                    "--board", self.board.id, "--out-dir", str(Path(directory) / "dist"),
                ],
                cwd=ROOT,
                text=True,
                capture_output=True,
                timeout=30,
            )
            self.assertEqual(result.returncode, 2)
            self.assertIn("build attestation validation failed", result.stderr)

            feature = Path(directory) / "feature.bin"
            feature.write_bytes(b"0.3.0 huskylens-sen0305 fake markers do not qualify")
            self.attest(feature, disabled_apps={"micropython"})
            feature_result = subprocess.run(
                [
                    sys.executable, str(TOOLS / "make_image.py"), str(feature),
                    "--board", self.board.id,
                    "--name", f"hackylens-full-{self.board.id}.bin",
                    "--out-dir", str(Path(directory) / "feature-dist"),
                ],
                cwd=ROOT,
                text=True,
                capture_output=True,
                timeout=30,
            )
            self.assertEqual(feature_result.returncode, 2)
            self.assertIn("release_qualified", feature_result.stderr)

    def test_image_output_name_and_metadata_paths_are_safe(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            image = temporary / "input.bin"
            image.write_bytes(b"qualified bytes")
            attestation = self.attest(image)
            for name in (
                "firmware.json", "../firmware.bin", "sub/firmware.bin",
                r"sub\firmware.bin", "C:firmware.bin", "NUL.bin",
            ):
                with self.subTest(name=name):
                    result = subprocess.run(
                        [
                            sys.executable, str(TOOLS / "make_image.py"), str(image),
                            "--attestation", str(attestation),
                            "--board", self.board.id, "--name", name,
                            "--out-dir", str(temporary / "dist"),
                        ],
                        cwd=ROOT, text=True, capture_output=True, timeout=30,
                    )
                    self.assertEqual(result.returncode, 2)
                    self.assertIn("safe portable basename", result.stderr)
            with self.assertRaisesRegex(
                firmware_sidecar.SidecarError, "must differ"
            ):
                firmware_sidecar.write(image, image, self.board)
            with self.assertRaisesRegex(
                firmware_attestation.AttestationError, "must differ"
            ):
                firmware_attestation.write(
                    image,
                    image,
                    self.board,
                    target="full",
                    disabled_apps=set(),
                    exclusions=[],
                    capabilities_sha256="a" * 64,
                )
            out_dir = temporary / "safe-root"
            out_dir.mkdir()
            with self.assertRaisesRegex(ValueError, "escapes --out-dir"):
                make_image.validate_output_paths(
                    out_dir,
                    (
                        out_dir / "safe.bin",
                        out_dir / "safe.json",
                        temporary / "outside.attestation.json",
                    ),
                )

    def test_package_to_flasher_sidecar_round_trip(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            source = temporary / f"hackylens-full-{self.board.id}.bin"
            source.write_bytes(b"firmware release bytes")
            source_sidecar = source.with_suffix(".json")
            firmware_sidecar.write(source_sidecar, source, self.board)
            source_attestation = self.attest(source)
            sdcard = temporary / "sdcard"
            sdcard.mkdir()
            (sdcard / "README.txt").write_text("test\n", encoding="utf-8")
            release = temporary / "release"

            def write_fake_notices(manifest: Path, archive: Path, _version: str) -> None:
                manifest.write_text("{}\n", encoding="utf-8")
                archive.write_bytes(b"notices")

            with mock.patch.object(
                package_release, "write_firmware_notices", side_effect=write_fake_notices
            ), contextlib.redirect_stdout(io.StringIO()):
                result = package_release.main([
                    "--board", self.board.id,
                    "--firmware", str(source),
                    "--sidecar", str(source_sidecar),
                    "--attestation", str(source_attestation),
                    "--sdcard", str(sdcard),
                    "--out-dir", str(release),
                ])
            self.assertEqual(result, 0)
            stem = f"hackylens-{self.board.id}-v0.4.0"
            release_image = release / f"{stem}.bin"
            release_sidecar = release / f"{stem}.json"
            release_manifest = release / f"{stem}-release.json"
            release_attestation = release / f"{stem}-attestation.json"
            self.assertTrue(release_manifest.is_file())
            firmware_attestation.read_and_validate(
                release_attestation,
                release_image,
                self.board,
                expected_target="full",
                require_release_qualified=True,
            )
            metadata = json.loads(release_sidecar.read_text(encoding="utf-8"))
            self.assertEqual(set(metadata), firmware_sidecar.REQUIRED_FIELDS | {"runtime_profile"})
            hkflash.validate_image_sidecar(
                release_image,
                self.args(sidecar=str(release_sidecar)),
            )

            source.write_bytes(source.read_bytes() + b"tampered")
            with mock.patch.object(
                package_release, "write_firmware_notices", side_effect=write_fake_notices
            ), self.assertRaisesRegex(SystemExit, "sidecar validation failed"):
                package_release.main([
                    "--board", self.board.id,
                    "--firmware", str(source),
                    "--sidecar", str(source_sidecar),
                    "--attestation", str(source_attestation),
                    "--sdcard", str(sdcard),
                    "--out-dir", str(temporary / "other-release"),
                ])

    def test_hmpy_contract_1_1_keeps_wire_major_1(self) -> None:
        protocol = (ROOT / "docs" / "HMPY_PROTOCOL.md").read_text(
            encoding="utf-8"
        )
        codec = (ROOT / "firmware" / "src" / "services" / "hmpy_codec.h").read_text(
            encoding="utf-8"
        )
        self.assertIn("version: 1.1.0", protocol)
        self.assertIn("wire-major: 1", protocol)
        self.assertIn("canonical `id`", protocol)
        self.assertIn("MUST NOT infer", protocol)
        self.assertIn("#define HMPY_PROTOCOL_VERSION 1U", codec)


class ResourceEvidenceTests(unittest.TestCase):
    def test_resource_policy_accepts_ram_savings_without_padding(self) -> None:
        acceptance = {
            "flash_delta_max_bytes": 8192,
            "static_ram_delta_max_bytes": 0,
            "new_background_tasks_queues_or_heap_allocations": 0,
        }
        self.assertTrue(
            check_phase1_resources.resource_budget_passes(8192, -176, [], acceptance)
        )
        self.assertFalse(
            check_phase1_resources.resource_budget_passes(8193, -176, [], acceptance)
        )
        self.assertFalse(
            check_phase1_resources.resource_budget_passes(0, 1, [], acceptance)
        )
        self.assertFalse(
            check_phase1_resources.resource_budget_passes(
                0, -176, ["new allocation"], acceptance
            )
        )

    def test_build_path_mapping_is_global_and_host_paths_are_rejected(self) -> None:
        source = (ROOT / "tools" / "build_firmware.py").read_text(encoding="utf-8")
        board_source = (
            ROOT / "boards" / "huskylens-sen0305" / "board.c"
        ).read_text(encoding="utf-8")
        self.assertIn("-DCMAKE_PROJECT_INCLUDE=", source)
        self.assertIn("-ffile-prefix-map=", source)
        self.assertNotIn("static_ram_compatibility_reserve", board_source)

        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            mapping = temporary / "path-map.cmake"
            sdk = temporary / "external-sdk"
            sdk.mkdir()
            build_firmware.write_reproducible_path_map(mapping, sdk)
            text = mapping.read_text(encoding="utf-8")
            self.assertIn("add_compile_options(", text)
            self.assertIn("/hackylens/sdk", text)
            self.assertIn("/hackylens/workspace", text)
            self.assertLess(
                text.index("/hackylens/workspace"),
                text.index("/hackylens/sdk"),
                "the specific SDK prefix map must follow the broad workspace map",
            )

            safe = temporary / "safe.bin"
            safe.write_bytes(b"/hackylens/workspace/firmware/main.c")
            build_firmware.reject_embedded_host_paths(safe, [ROOT, sdk])
            unsafe = temporary / "unsafe.bin"
            unsafe.write_bytes(str(ROOT.resolve()).encode("utf-8"))
            with self.assertRaisesRegex(RuntimeError, "embeds host path"):
                build_firmware.reject_embedded_host_paths(unsafe, [ROOT, sdk])

    def test_baseline_identity_repository_and_bootstrap_provenance_are_strict(self) -> None:
        baseline_path = ROOT / "docs" / "evidence" / "phase1-baseline.json"
        document = check_phase1_resources.load_baseline(baseline_path)
        baseline = document["baseline"]
        self.assertIsInstance(baseline, dict)
        self.assertEqual(
            check_phase1_resources.sha256(baseline_path),
            check_phase1_resources.PINNED_BASELINE_SHA256,
        )
        attributes = (ROOT / ".gitattributes").read_text(encoding="utf-8")
        self.assertIn("docs/evidence/phase1-*.json text eol=lf", attributes)
        check_phase1_resources.verify_baseline_repository_provenance(document)

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "baseline.json"
            wrong_type = copy.deepcopy(document)
            wrong_type["baseline"]["raw_image_bytes"] = True
            with self.assertRaisesRegex(RuntimeError, "expected integer"):
                check_phase1_resources.validate_baseline_document(wrong_type)

            wrong_formula = copy.deepcopy(document)
            wrong_formula["formulas"]["static_ram"] = "bss_bytes"
            with self.assertRaisesRegex(RuntimeError, "formulas"):
                check_phase1_resources.validate_baseline_document(wrong_formula)

            unknown = copy.deepcopy(document)
            unknown["baseline"]["invented"] = 1
            with self.assertRaisesRegex(RuntimeError, "unknown"):
                check_phase1_resources.validate_baseline_document(unknown)

            path.write_bytes(check_phase1_resources.canonical_json_bytes(wrong_formula))
            with self.assertRaisesRegex(RuntimeError, "provenance digest mismatch"):
                check_phase1_resources.load_baseline(path)

        wrong_version = copy.deepcopy(document)
        wrong_version["baseline"]["firmware_version"] = "9.9.9"
        with self.assertRaisesRegex(RuntimeError, "commit VERSION"):
            check_phase1_resources.verify_baseline_repository_provenance(wrong_version)

        wrong_sdk = copy.deepcopy(document)
        wrong_sdk["toolchain"]["kendryte_standalone_sdk_revision"] = "f" * 40
        with self.assertRaisesRegex(RuntimeError, "SDK revision"):
            check_phase1_resources.verify_baseline_repository_provenance(wrong_sdk)

        wrong_toolchain = copy.deepcopy(document)
        wrong_toolchain["toolchain"]["archive_sha256"] = "f" * 64
        with self.assertRaisesRegex(RuntimeError, "archive SHA-256"):
            check_phase1_resources.verify_baseline_repository_provenance(wrong_toolchain)

        wrong_toolchain_version = copy.deepcopy(document)
        wrong_toolchain_version["toolchain"]["kendryte_toolchain"] = "v0.0.0"
        with self.assertRaisesRegex(RuntimeError, "toolchain version"):
            check_phase1_resources.verify_baseline_repository_provenance(
                wrong_toolchain_version
            )

        with self.assertRaisesRegex(RuntimeError, "unavailable"):
            check_phase1_resources.ensure_commit_available("0" * 40)
        with self.assertRaisesRegex(RuntimeError, "not an ancestor"):
            check_phase1_resources.ensure_commit_is_ancestor("0" * 40)

    def test_complete_runtime_object_snapshots_cover_headers_multiline_wrappers_and_new(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repository = Path(directory)
            subprocess.run(["git", "init", "--quiet"], cwd=repository, check=True)
            source = repository / "firmware" / "preexisting.c"
            source.parent.mkdir(parents=True)
            source.write_text(
                "void *preexisting(void) { return malloc(8); }\n",
                encoding="utf-8",
            )
            subprocess.run(["git", "add", "firmware/preexisting.c"], cwd=repository, check=True)
            subprocess.run(
                [
                    "git", "-c", "user.name=Resource Test",
                    "-c", "user.email=resource@example.invalid",
                    "commit", "--quiet", "-m", "baseline",
                ],
                cwd=repository,
                check=True,
            )
            commit = subprocess.run(
                ["git", "rev-parse", "HEAD"], cwd=repository, check=True,
                text=True, capture_output=True,
            ).stdout.strip()
            check_phase1_resources.ensure_tracked_result(
                source, root=repository
            )
            moved = repository / "firmware" / "renamed.c"
            source.rename(moved)
            header = repository / "firmware" / "new_runtime.hpp"
            header.write_text(
                "/* malloc(999); xTaskCreate(fake); new Fake; */\n"
                "const char *ignored = \"strdup and std::make_shared<Fake>()\";\n"
                "#define MAKE_BUFFER() \\\n"
                "  malloc(4)\n"
                "static Widget *global_widget = new Widget;\n"
                "inline void *new_allocating_wrapper() {\n"
                "  project_alloc_buffer\n"
                "    (64);\n"
                "  xQueueCreate\n"
                "    (2, 4);\n"
                "  void *(*allocate_alias)(size_t) = malloc;\n"
                "  allocate_alias(8);\n"
                "  char *copy = strdup(\"value\");\n"
                "  auto unique = std::make_unique<Widget>();\n"
                "  auto shared = std::make_shared<Widget>();\n"
                "  void *raw = ::operator new(16);\n"
                "  std::thread worker(run_worker);\n"
                "  MAKE_BUFFER();\n"
                "  return new Widget;\n"
                "}\n",
                encoding="utf-8",
            )
            caller = repository / "firmware" / "new_caller.c"
            caller.write_text(
                "void *new_allocating_wrapper(void);\n"
                "extern void *(*cross_tu_alias)(size_t);\n"
                "void *call_new_wrapper(void) {\n"
                "  cross_tu_alias(12);\n"
                "  return new_allocating_wrapper();\n"
                "}\n"
                "static void *global_buffer = malloc(4);\n",
                encoding="utf-8",
            )
            alias_tu = repository / "firmware" / "alias.c"
            alias_tu.write_text(
                "void *(*cross_tu_alias)(size_t) = malloc;\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(RuntimeError, "not tracked"):
                check_phase1_resources.ensure_tracked_result(header, root=repository)
            findings = check_phase1_resources.added_runtime_objects(
                commit, root=repository
            )
            joined = "\n".join(findings)
            self.assertNotIn("call:malloc occurrences increased", joined)
            self.assertIn("call:project_alloc_buffer", joined)
            self.assertIn("call:xQueueCreate", joined)
            self.assertIn("alias:allocate_alias", joined)
            self.assertIn("call:allocate_alias", joined)
            self.assertIn("call:cross_tu_alias", joined)
            self.assertIn("call:strdup", joined)
            self.assertIn("call:std::make_unique", joined)
            self.assertIn("call:std::make_shared", joined)
            self.assertIn("alias:MAKE_BUFFER", joined)
            self.assertIn("call:MAKE_BUFFER", joined)
            self.assertIn("call:std::thread", joined)
            self.assertIn("call:new_allocating_wrapper", joined)
            self.assertIn("cxx:new", joined)
            self.assertTrue(
                any("cxx:new" in finding and "in <file>" in finding
                    for finding in findings),
                "file-scope C++ new must be a runtime-object site",
            )
            self.assertTrue(
                any("call:malloc" in finding and "in <file>" in finding
                    for finding in findings),
                "file-scope direct allocation must be a runtime-object site",
            )
            self.assertIn("firmware/new_runtime.hpp", joined)
            self.assertNotIn("ignored", joined)

    def test_runtime_object_site_identity_catches_delete_add_same_count(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repository = Path(directory)
            subprocess.run(["git", "init", "--quiet"], cwd=repository, check=True)
            old = repository / "firmware" / "old.c"
            old.parent.mkdir(parents=True)
            old.write_text(
                "void *old_buffer(void) { return malloc(8); }\n",
                encoding="utf-8",
            )
            subprocess.run(["git", "add", "firmware/old.c"], cwd=repository, check=True)
            subprocess.run(
                [
                    "git", "-c", "user.name=Resource Test",
                    "-c", "user.email=resource@example.invalid",
                    "commit", "--quiet", "-m", "baseline",
                ],
                cwd=repository,
                check=True,
            )
            commit = subprocess.run(
                ["git", "rev-parse", "HEAD"], cwd=repository, check=True,
                text=True, capture_output=True,
            ).stdout.strip()
            caller = repository / "firmware" / "caller.c"
            caller.write_text(
                "void *old_buffer(void);\n"
                "void *use_old_wrapper(void) { return old_buffer(); }\n",
                encoding="utf-8",
            )
            wrapper_findings = check_phase1_resources.added_runtime_objects(
                commit, root=repository
            )
            self.assertIn("call:old_buffer", "\n".join(wrapper_findings))
            caller.unlink()
            renamed = repository / "firmware" / "renamed.c"
            old.rename(renamed)
            self.assertEqual(
                check_phase1_resources.added_runtime_objects(
                    commit, root=repository
                ),
                [],
                "an exact file rename keeps its grandfathered site",
            )
            renamed.write_text(
                "void *replacement_buffer(void) { return malloc(8); }\n",
                encoding="utf-8",
            )
            findings = check_phase1_resources.added_runtime_objects(
                commit, root=repository
            )
            self.assertTrue(findings)
            self.assertIn("firmware/renamed.c", "\n".join(findings))

    def test_runtime_object_site_context_rejects_same_fingerprint_replacement(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repository = Path(directory)
            subprocess.run(["git", "init", "--quiet"], cwd=repository, check=True)
            source = repository / "firmware" / "site.c"
            source.parent.mkdir(parents=True)
            source.write_text(
                "void *choose(int enabled) {\n"
                "  if (enabled) return malloc(4);\n"
                "  return 0;\n"
                "}\n",
                encoding="utf-8",
            )
            subprocess.run(["git", "add", "firmware/site.c"], cwd=repository, check=True)
            subprocess.run(
                [
                    "git", "-c", "user.name=Resource Test",
                    "-c", "user.email=resource@example.invalid",
                    "commit", "--quiet", "-m", "baseline",
                ],
                cwd=repository,
                check=True,
            )
            commit = subprocess.run(
                ["git", "rev-parse", "HEAD"], cwd=repository, check=True,
                text=True, capture_output=True,
            ).stdout.strip()

            # The call expression and source line remain identical; only the
            # condition changes.  It is a replacement site, not grandfathered
            # baseline evidence.
            source.write_text(
                "void *choose(int enabled) {\n"
                "  if (!enabled) return malloc(4);\n"
                "  return 0;\n"
                "}\n",
                encoding="utf-8",
            )
            findings = check_phase1_resources.added_runtime_objects(
                commit, root=repository
            )
            self.assertIn("call:malloc", "\n".join(findings))

    def test_runtime_object_header_macro_new_cross_tu_use_is_detected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repository = Path(directory)
            subprocess.run(["git", "init", "--quiet"], cwd=repository, check=True)
            header = repository / "firmware" / "allocate.h"
            header.parent.mkdir(parents=True)
            header.write_text(
                "#define MAKE() \\\n"
                "  malloc(4)\n",
                encoding="utf-8",
            )
            subprocess.run(["git", "add", "firmware/allocate.h"], cwd=repository, check=True)
            subprocess.run(
                [
                    "git", "-c", "user.name=Resource Test",
                    "-c", "user.email=resource@example.invalid",
                    "commit", "--quiet", "-m", "baseline",
                ],
                cwd=repository,
                check=True,
            )
            commit = subprocess.run(
                ["git", "rev-parse", "HEAD"], cwd=repository, check=True,
                text=True, capture_output=True,
            ).stdout.strip()
            caller = repository / "firmware" / "caller.c"
            caller.write_text(
                '#include "allocate.h"\n'
                "void *new_use(void) { return MAKE(); }\n",
                encoding="utf-8",
            )
            findings = check_phase1_resources.added_runtime_objects(
                commit, root=repository
            )
            self.assertIn("call:MAKE", "\n".join(findings))

    def test_result_freshness_projection_checks_metrics_and_hash_types(self) -> None:
        path = ROOT / "docs" / "evidence" / "phase1-result.json"
        document = check_phase1_resources.validate_result_document(
            json.loads(path.read_text(encoding="utf-8"))
        )
        alternate = copy.deepcopy(document)
        self.assertEqual(
            document["hash_scope"],
            check_phase1_resources.LOCAL_HASH_SCOPE,
        )
        alternate["elf"]["local_sha256"] = "a" * 64
        self.assertEqual(
            check_phase1_resources.deterministic_result_projection(document),
            check_phase1_resources.deterministic_result_projection(alternate),
        )
        alternate["elf"]["data_bytes"] += 1
        self.assertNotEqual(
            check_phase1_resources.deterministic_result_projection(document),
            check_phase1_resources.deterministic_result_projection(alternate),
        )
        malformed = copy.deepcopy(document)
        malformed["image"]["local_sha256"] = "not-a-hash"
        with self.assertRaisesRegex(RuntimeError, "local_sha256"):
            check_phase1_resources.validate_result_document(malformed)
        generic_hash = copy.deepcopy(document)
        generic_hash["image"]["sha256"] = generic_hash["image"].pop("local_sha256")
        with self.assertRaisesRegex(RuntimeError, "missing or unknown"):
            check_phase1_resources.validate_result_document(generic_hash)

    def test_phase1_hardware_evidence_is_canonical_sanitized_and_hash_bound(self) -> None:
        path = ROOT / "docs" / "evidence" / "phase1-hardware-smoke.json"
        encoded = path.read_bytes()
        document = json.loads(encoded.decode("utf-8"))
        self.assertEqual(
            encoded, firmware_attestation.canonical_json_bytes(document)
        )
        self.assertEqual(document["schema"], 1)
        self.assertTrue(document["accepted"])
        self.assertEqual(document["board"], "huskylens-sen0305")
        self.assertEqual(document["firmware"]["version"], "0.3.0")
        self.assertFalse(document["privacy"]["host_port_recorded"])
        self.assertFalse(document["privacy"]["usb_serial_recorded"])
        self.assertNotIn(b"COM10", encoded)

        closure_link = document["closure_result"]
        self.assertEqual(set(closure_link), {"path", "sha256"})
        self.assertEqual(
            closure_link["path"],
            "docs/evidence/phase1-closure-result.json",
        )
        self.assertNotEqual(
            closure_link["path"], "docs/evidence/phase1-result.json"
        )
        closure_path = (ROOT / closure_link["path"]).resolve()
        self.assertEqual(
            closure_path.parent, (ROOT / "docs" / "evidence").resolve()
        )
        closure_encoded = closure_path.read_bytes()
        self.assertEqual(
            hashlib.sha256(closure_encoded).hexdigest(),
            closure_link["sha256"],
        )
        closure = check_phase1_resources.validate_result_document(
            json.loads(closure_encoded.decode("utf-8"))
        )
        self.assertEqual(
            closure_encoded,
            firmware_attestation.canonical_json_bytes(closure),
        )
        self.assertTrue(closure["accepted"])
        self.assertEqual(document["board"], closure["board"])
        self.assertEqual(
            document["firmware"]["version"], closure["firmware_version"]
        )
        self.assertEqual(
            document["firmware"]["image_bytes"],
            closure["image"]["raw_bytes"],
        )
        self.assertEqual(
            document["firmware"]["image_sha256"],
            closure["image"]["local_sha256"],
        )
        self.assertEqual(
            set(document["results"]),
            {
                "boot", "buttons", "camera", "display", "external_links",
                "flash_package_round_trip", "hmpy", "lights", "sd",
            },
        )
        self.assertTrue(
            all(result["status"] == "pass"
                for result in document["results"].values())
        )
        for artifact in document["visual_artifacts"]:
            artifact_path = ROOT / artifact["path"]
            content = artifact_path.read_bytes()
            self.assertTrue(content.startswith(b"\x89PNG\r\n\x1a\n"))
            self.assertEqual(hashlib.sha256(content).hexdigest(), artifact["sha256"])
            self.assertEqual(
                int.from_bytes(content[16:20], "big"), artifact["width"]
            )
            self.assertEqual(
                int.from_bytes(content[20:24], "big"), artifact["height"]
            )


if __name__ == "__main__":
    unittest.main()
