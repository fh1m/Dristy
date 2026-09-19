#!/usr/bin/env python3
"""Strict private loader for the experimental K210 Board Port Contract."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re
try:
    import tomllib
except ModuleNotFoundError:  # Python 3.10 and older
    import tomli as tomllib
from typing import Any, Mapping


ROOT = Path(__file__).resolve().parents[1]
BOARDS_DIR = ROOT / "boards"
REGISTRY_PATH = ROOT / "platforms" / "k210" / "devices.toml"
BOARD_ID_RE = re.compile(r"^[a-z][a-z0-9]*(?:-[a-z0-9]+)*$")
IDENTIFIER_RE = BOARD_ID_RE
MACRO_RE = re.compile(r"^[A-Z][A-Z0-9_]*$")
VID_RE = re.compile(r"^[0-9A-F]{4}$")

ROOT_FIELDS = {
    "schema", "id", "platform", "support", "releaseable",
    "runtime_profile", "devices", "routes", "route_selections",
    "defaults", "connectors", "programming",
}
DEVICE_FIELDS = {"id", "kind", "driver", "support"}
ROUTE_FIELDS = {
    "id", "macro", "pin", "function", "peripheral",
    "exclusive_group", "runtime_mux_group", "compile",
}
CONNECTOR_FIELDS = {"id", "kind", "pins", "protocols", "routes"}
PROGRAMMING_FIELDS = {
    "supported", "reset_profile", "reboot_profile", "isp_stub",
    "flash_type", "flash_mode", "boot_baud", "flash_baud",
    "reset_attempts", "usb_detection",
}
USB_FIELDS = {"mode", "vids"}
DEFAULT_FIELDS = {
    "lcd_width", "lcd_height", "lcd_spi", "lcd_chip_select", "lcd_spi_hz",
    "gpiohs_lcd_reset", "gpiohs_lcd_dc", "gpiohs_button_left",
    "gpiohs_button_ok", "gpiohs_button_right", "gpiohs_button_back",
    "gpiohs_sd_chip_select", "sd_spi", "sd_chip_select", "flash_spi",
    "flash_chip_select", "led_pwm_device", "led_pwm_channel",
    "rgb_pwm_device", "rgb_pwm_channel0", "rgb_pwm_channel1",
    "rgb_pwm_channel2", "screen_pwm_device", "screen_pwm_channel",
    "pwm_frequency_hz", "camera_sccb_address", "camera_xclk_hz",
    "camera_sccb_hz", "camera_max_width", "camera_max_height",
}
GPIOHS_DEFAULT_ROUTES = {
    "gpiohs_lcd_reset": "IO_LCD_RST",
    "gpiohs_lcd_dc": "IO_LCD_DC_OR_AUX",
    "gpiohs_button_left": "IO_BTN_LEFT",
    "gpiohs_button_ok": "IO_BTN_OK",
    "gpiohs_button_right": "IO_BTN_RIGHT",
    "gpiohs_button_back": "IO_BTN_BACK",
    "gpiohs_sd_chip_select": "IO_SD_CS",
}
SPI_DEFAULT_ROUTES = {
    "lcd_spi": "IO_LCD_SCLK",
    "sd_spi": "IO_SD_SCLK",
}
SPI_CHIP_SELECT_DEFAULT_ROUTES = {
    "lcd_chip_select": "IO_LCD_CS",
}
DEFAULT_RANGES = {
    "lcd_width": (1, 640),
    "lcd_height": (1, 480),
    "lcd_spi": (0, 3),
    "lcd_chip_select": (0, 3),
    "lcd_spi_hz": (1, 100_000_000),
    "gpiohs_lcd_reset": (0, 31),
    "gpiohs_lcd_dc": (0, 31),
    "gpiohs_button_left": (0, 31),
    "gpiohs_button_ok": (0, 31),
    "gpiohs_button_right": (0, 31),
    "gpiohs_button_back": (0, 31),
    "gpiohs_sd_chip_select": (0, 31),
    "sd_spi": (0, 3),
    "sd_chip_select": (0, 3),
    "flash_spi": (0, 3),
    "flash_chip_select": (0, 3),
    "led_pwm_device": (0, 2),
    "led_pwm_channel": (0, 3),
    "rgb_pwm_device": (0, 2),
    "rgb_pwm_channel0": (0, 3),
    "rgb_pwm_channel1": (0, 3),
    "rgb_pwm_channel2": (0, 3),
    "screen_pwm_device": (0, 2),
    "screen_pwm_channel": (0, 3),
    "pwm_frequency_hz": (1, 100_000_000),
    "camera_sccb_address": (1, 0x7F),
    "camera_xclk_hz": (1, 50_000_000),
    "camera_sccb_hz": (1, 1_000_000),
    "camera_max_width": (1, 640),
    "camera_max_height": (1, 480),
}
PWM_DEFAULT_ROUTES = (
    ("led_pwm_device", "led_pwm_channel", "IO_LED_ILLUM"),
    ("rgb_pwm_device", "rgb_pwm_channel0", "IO_RGB_PWM0"),
    ("rgb_pwm_device", "rgb_pwm_channel1", "IO_RGB_PWM1"),
    ("rgb_pwm_device", "rgb_pwm_channel2", "IO_RGB_PWM2"),
    ("screen_pwm_device", "screen_pwm_channel", "IO_LCD_BL"),
)
K210_FIXED_DEFAULTS = {
    # The K210 boot flash controller is the dedicated SPI3/SS0 instance; this
    # is a platform primitive rather than board pin-routing metadata.
    "flash_spi": 3,
    "flash_chip_select": 0,
}
REGISTRY_FIELDS = {
    "schema", "platform", "devices", "functions", "reset_profiles",
    "reboot_profiles", "usb_detection_modes", "flash_types", "flash_modes",
    "peripheral_instances", "prepare_callbacks", "runtime_profiles",
    "runtime_base_devices", "route_roles", "legacy_runtime_muxes",
}
REGISTRY_DEVICE_FIELDS = {"kind", "allowed_drivers"}
REGISTRY_FUNCTION_FIELDS = {"peripheral", "macro"}
REGISTRY_ROUTE_ROLE_FIELDS = {"function", "peripheral"}
REGISTRY_LEGACY_MUX_FIELDS = {"routes"}
REGISTRY_LEGACY_MUX_ROUTE_FIELDS = {"macro", "function", "pin", "peripheral"}


class ContractError(ValueError):
    """A descriptor or platform-registry contract violation."""


def _load_toml(path: Path) -> dict[str, Any]:
    try:
        with path.open("rb") as source:
            value = tomllib.load(source)
    except (OSError, tomllib.TOMLDecodeError) as exc:
        raise ContractError(f"cannot read {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ContractError(f"{path}: root must be a table")
    return value


def _strict(table: Mapping[str, Any], allowed: set[str], field: str) -> None:
    unknown = sorted(set(table) - allowed)
    if unknown:
        raise ContractError(f"{field}: unknown field(s): {', '.join(unknown)}")


def _string(value: Any, field: str, *, pattern: re.Pattern[str] | None = None) -> str:
    if not isinstance(value, str) or not value:
        raise ContractError(f"{field}: expected non-empty string")
    if pattern is not None and pattern.fullmatch(value) is None:
        raise ContractError(f"{field}: invalid identifier {value!r}")
    return value


def _boolean(value: Any, field: str) -> bool:
    if not isinstance(value, bool):
        raise ContractError(f"{field}: expected boolean")
    return value


def _integer(value: Any, field: str, *, minimum: int = 0,
             maximum: int = 0xFFFFFFFF) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise ContractError(f"{field}: expected integer")
    if value < minimum or value > maximum:
        raise ContractError(f"{field}: outside {minimum}..{maximum}")
    return value


def _string_list(value: Any, field: str) -> list[str]:
    if not isinstance(value, list) or any(not isinstance(item, str) or not item for item in value):
        raise ContractError(f"{field}: expected an array of non-empty strings")
    if len(value) != len(set(value)):
        raise ContractError(f"{field}: duplicate value")
    return value


@dataclass(frozen=True)
class PlatformRegistry:
    path: Path
    data: dict[str, Any]

    @property
    def platform(self) -> str:
        return self.data["platform"]

    def values(self, key: str) -> set[str]:
        if key == "device_ids":
            return set(self.data["devices"])
        if key == "device_kinds":
            return {entry["kind"] for entry in self.data["devices"].values()}
        if key == "drivers":
            return {
                driver
                for entry in self.data["devices"].values()
                for driver in entry["allowed_drivers"]
            }
        if key == "fpioa_functions":
            return set(self.data["functions"])
        if key == "peripheral_instances":
            return set(self.data["peripheral_instances"])
        return set(self.data[key])

    def device(self, device_id: str) -> dict[str, Any]:
        return self.data["devices"][device_id]

    def function(self, function_id: str) -> dict[str, str]:
        return self.data["functions"][function_id]

    def function_macro(self, function_id: str) -> str:
        return self.function(function_id)["macro"]

    def device_kinds(self) -> list[str]:
        """Return registered inventory kinds in stable registry order."""
        result: list[str] = []
        for entry in self.data["devices"].values():
            if entry["kind"] not in result:
                result.append(entry["kind"])
        return result


@dataclass(frozen=True)
class Board:
    path: Path
    data: dict[str, Any]
    registry: PlatformRegistry

    @property
    def directory(self) -> Path:
        return self.path.parent

    @property
    def id(self) -> str:
        return self.data["id"]

    @property
    def support(self) -> str:
        return self.data["support"]

    @property
    def releaseable(self) -> bool:
        return self.data["releaseable"]

    @property
    def runtime_profile(self) -> str | None:
        return self.data.get("runtime_profile")

    @property
    def programming(self) -> dict[str, Any]:
        return self.data["programming"]

    @property
    def flash_layout_path(self) -> Path:
        return self.directory / "flash_layout.json"

    @property
    def generated_dir(self) -> Path:
        return self.directory / "generated"

    def driver_supported_devices(self) -> list[dict[str, Any]]:
        return [item for item in self.data["devices"] if item["support"] == "driver"]

    def driver_supported_kinds(self) -> set[str]:
        return {item["kind"] for item in self.driver_supported_devices()}

    def selected_routes(self) -> list[dict[str, Any]]:
        selections = self.data.get("route_selections", {})
        result = []
        for route in self.data["routes"]:
            if not route.get("compile", True):
                continue
            group = route.get("exclusive_group")
            if group and selections.get(group) != route["id"]:
                continue
            result.append(route)
        return result

    def required_callbacks(self) -> set[str]:
        callback_map = self.registry.data["prepare_callbacks"]
        return {
            callback_map[kind] for kind in self.driver_supported_kinds()
            if kind in callback_map
        }


def load_registry(path: Path = REGISTRY_PATH) -> PlatformRegistry:
    data = _load_toml(path)
    _strict(data, REGISTRY_FIELDS, "registry")
    if type(data.get("schema")) is not int or data["schema"] != 1:
        raise ContractError("registry.schema: expected 1")
    platform = _string(data.get("platform"), "registry.platform", pattern=IDENTIFIER_RE)
    for key in (
        "reset_profiles", "reboot_profiles", "usb_detection_modes",
        "flash_types", "flash_modes", "peripheral_instances",
    ):
        _string_list(data.get(key), f"registry.{key}")
        for value in data[key]:
            _string(value, f"registry.{key}", pattern=IDENTIFIER_RE)

    devices = data.get("devices")
    if not isinstance(devices, dict) or not devices:
        raise ContractError("registry.devices: expected non-empty table")
    for device_id, entry in devices.items():
        field = f"registry.devices.{device_id}"
        _string(device_id, "registry.devices key", pattern=IDENTIFIER_RE)
        if not isinstance(entry, dict):
            raise ContractError(f"{field}: expected table")
        _strict(entry, REGISTRY_DEVICE_FIELDS, field)
        _string(entry.get("kind"), f"{field}.kind", pattern=IDENTIFIER_RE)
        _string_list(entry.get("allowed_drivers"), f"{field}.allowed_drivers")
        for driver in entry["allowed_drivers"]:
            _string(driver, f"{field}.allowed_drivers", pattern=IDENTIFIER_RE)

    functions = data.get("functions")
    if not isinstance(functions, dict) or not functions:
        raise ContractError("registry.functions: expected non-empty table")
    function_macros: set[str] = set()
    for function_id, entry in functions.items():
        field = f"registry.functions.{function_id}"
        _string(function_id, "registry.functions key", pattern=IDENTIFIER_RE)
        if not isinstance(entry, dict):
            raise ContractError(f"{field}: expected table")
        _strict(entry, REGISTRY_FUNCTION_FIELDS, field)
        peripheral = _string(
            entry.get("peripheral"), f"{field}.peripheral", pattern=IDENTIFIER_RE
        )
        if peripheral not in data["peripheral_instances"]:
            raise ContractError(f"{field}.peripheral: unknown instance {peripheral!r}")
        macro = _string(entry.get("macro"), f"{field}.macro", pattern=MACRO_RE)
        if macro in function_macros:
            raise ContractError(f"{field}.macro: duplicate {macro!r}")
        function_macros.add(macro)

    route_roles = data.get("route_roles")
    if not isinstance(route_roles, dict) or not route_roles:
        raise ContractError("registry.route_roles: expected non-empty table")
    for macro, entry in route_roles.items():
        field = f"registry.route_roles.{macro}"
        _string(macro, "registry.route_roles key", pattern=MACRO_RE)
        if not isinstance(entry, dict):
            raise ContractError(f"{field}: expected table")
        _strict(entry, REGISTRY_ROUTE_ROLE_FIELDS, field)
        function = _string(
            entry.get("function"), f"{field}.function", pattern=IDENTIFIER_RE
        )
        if function not in functions:
            raise ContractError(f"{field}.function: unknown function {function!r}")
        peripheral = _string(
            entry.get("peripheral"), f"{field}.peripheral", pattern=IDENTIFIER_RE
        )
        expected_peripheral = functions[function]["peripheral"]
        if peripheral != expected_peripheral:
            raise ContractError(
                f"{field}.peripheral: function {function!r} requires "
                f"{expected_peripheral!r}"
            )

    legacy_muxes = data.get("legacy_runtime_muxes")
    if not isinstance(legacy_muxes, dict):
        raise ContractError("registry.legacy_runtime_muxes: expected table")
    for group, entry in legacy_muxes.items():
        field = f"registry.legacy_runtime_muxes.{group}"
        _string(group, "registry.legacy_runtime_muxes key", pattern=IDENTIFIER_RE)
        if not isinstance(entry, dict):
            raise ContractError(f"{field}: expected table")
        _strict(entry, REGISTRY_LEGACY_MUX_FIELDS, field)
        candidates = entry.get("routes")
        if not isinstance(candidates, list) or len(candidates) != 4:
            raise ContractError(
                f"{field}.routes: legacy two-mode mux requires exactly four routes"
            )
        normalized: set[tuple[str, str, int, str]] = set()
        for index, candidate in enumerate(candidates):
            candidate_field = f"{field}.routes[{index}]"
            if not isinstance(candidate, dict):
                raise ContractError(f"{candidate_field}: expected table")
            _strict(candidate, REGISTRY_LEGACY_MUX_ROUTE_FIELDS, candidate_field)
            macro = _string(
                candidate.get("macro"), f"{candidate_field}.macro", pattern=MACRO_RE
            )
            function = _string(
                candidate.get("function"), f"{candidate_field}.function",
                pattern=IDENTIFIER_RE,
            )
            pin = _integer(candidate.get("pin"), f"{candidate_field}.pin", maximum=47)
            peripheral = _string(
                candidate.get("peripheral"), f"{candidate_field}.peripheral",
                pattern=IDENTIFIER_RE,
            )
            role = route_roles.get(macro)
            if (role is None or role["function"] != function or
                    role["peripheral"] != peripheral):
                raise ContractError(
                    f"{candidate_field}: must match registry route role {macro!r}"
                )
            normalized.add((macro, function, pin, peripheral))
        if len(normalized) != len(candidates):
            raise ContractError(f"{field}.routes: duplicate route definition")
        pins = [candidate[2] for candidate in normalized]
        peripherals = [candidate[3] for candidate in normalized]
        pin_set = set(pins)
        peripheral_set = set(peripherals)
        if (len(pin_set) != 2 or any(pins.count(pin) != 2 for pin in pin_set) or
                len(peripheral_set) != 2 or
                any(peripherals.count(item) != 2 for item in peripheral_set)):
            raise ContractError(
                f"{field}.routes: expected two symmetric modes on exactly two pins"
            )
        for peripheral in peripheral_set:
            if {
                candidate[2] for candidate in normalized if candidate[3] == peripheral
            } != pin_set:
                raise ContractError(
                    f"{field}.routes: each mode must cover the same exact two pins"
                )

    registry = PlatformRegistry(path=path, data=data)
    callbacks = data.get("prepare_callbacks")
    if not isinstance(callbacks, dict):
        raise ContractError("registry.prepare_callbacks: expected table")
    if not set(callbacks).issubset(registry.values("device_kinds")):
        raise ContractError("registry.prepare_callbacks: unknown device kind")
    for key, value in callbacks.items():
        _string(value, f"registry.prepare_callbacks.{key}")
    profiles = data.get("runtime_profiles")
    if not isinstance(profiles, dict) or not profiles:
        raise ContractError("registry.runtime_profiles: expected non-empty table")
    runtime_base = _string_list(
        data.get("runtime_base_devices"), "registry.runtime_base_devices"
    )
    if not runtime_base:
        raise ContractError("registry.runtime_base_devices: expected non-empty array")
    unknown_base = sorted(set(runtime_base) - registry.values("device_kinds"))
    if unknown_base:
        raise ContractError(
            "registry.runtime_base_devices: unknown device kind(s): "
            + ", ".join(unknown_base)
        )
    for name, profile in profiles.items():
        _string(name, "registry.runtime_profiles key", pattern=IDENTIFIER_RE)
        if not isinstance(profile, dict):
            raise ContractError(f"registry.runtime_profiles.{name}: expected table")
        _strict(profile, {"required_devices"}, f"registry.runtime_profiles.{name}")
        required = _string_list(
            profile.get("required_devices"),
            f"registry.runtime_profiles.{name}.required_devices",
        )
        unknown = sorted(set(required) - registry.values("device_kinds"))
        if unknown:
            raise ContractError(
                f"registry.runtime_profiles.{name}: unknown device kind(s): " + ", ".join(unknown)
            )
        missing_base = sorted(set(runtime_base) - set(required))
        if missing_base:
            raise ContractError(
                f"registry.runtime_profiles.{name}: missing runtime base device(s): "
                + ", ".join(missing_base)
            )
    data["platform"] = platform
    return registry


def _validate_devices(data: dict[str, Any], registry: PlatformRegistry) -> None:
    devices = data.get("devices")
    if not isinstance(devices, list) or not devices:
        raise ContractError("devices: expected a non-empty array of tables")
    ids: set[str] = set()
    kinds: set[str] = set()
    for index, device in enumerate(devices):
        field = f"devices[{index}]"
        if not isinstance(device, dict):
            raise ContractError(f"{field}: expected table")
        _strict(device, DEVICE_FIELDS, field)
        device_id = _string(device.get("id"), f"{field}.id", pattern=IDENTIFIER_RE)
        kind = _string(device.get("kind"), f"{field}.kind", pattern=IDENTIFIER_RE)
        support = device.get("support")
        if device_id not in registry.values("device_ids"):
            raise ContractError(f"{field}.id: unknown device ID {device_id!r}")
        registered = registry.device(device_id)
        if kind != registered["kind"]:
            raise ContractError(
                f"{field}.kind: device {device_id!r} requires {registered['kind']!r}"
            )
        if device_id in ids:
            raise ContractError(f"{field}.id: duplicate {device_id!r}")
        ids.add(device_id)
        if kind in kinds:
            raise ContractError(f"{field}.kind: duplicate inventory role {kind!r}")
        kinds.add(kind)
        if support not in {"driver", "known-unsupported"}:
            raise ContractError(f"{field}.support: expected driver or known-unsupported")
        driver = device.get("driver")
        if support == "driver":
            if not isinstance(driver, str) or driver not in registered["allowed_drivers"]:
                raise ContractError(
                    f"{field}.driver: not allowed for device {device_id!r}"
                )
        elif driver is not None:
            raise ContractError(f"{field}.driver: forbidden for known-unsupported device")


def _validate_routes(data: dict[str, Any], registry: PlatformRegistry) -> None:
    routes = data.get("routes")
    if not isinstance(routes, list):
        raise ContractError("routes: expected array of tables")
    ids: set[str] = set()
    macro_routes: dict[str, dict[str, Any]] = {}
    groups: dict[str, list[dict[str, Any]]] = {}
    runtime_mux_groups: dict[str, list[dict[str, Any]]] = {}
    for index, route in enumerate(routes):
        field = f"routes[{index}]"
        if not isinstance(route, dict):
            raise ContractError(f"{field}: expected table")
        _strict(route, ROUTE_FIELDS, field)
        route_id = _string(route.get("id"), f"{field}.id", pattern=IDENTIFIER_RE)
        macro = _string(route.get("macro"), f"{field}.macro", pattern=MACRO_RE)
        _integer(route.get("pin"), f"{field}.pin", maximum=47)
        function = _string(route.get("function"), f"{field}.function", pattern=IDENTIFIER_RE)
        peripheral = _string(route.get("peripheral"), f"{field}.peripheral", pattern=IDENTIFIER_RE)
        if function not in registry.values("fpioa_functions"):
            raise ContractError(f"{field}.function: unknown FPIOA function {function!r}")
        registered_function = registry.function(function)
        if peripheral != registered_function["peripheral"]:
            raise ContractError(
                f"{field}.peripheral: function {function!r} requires "
                f"{registered_function['peripheral']!r}"
            )
        role = registry.data["route_roles"].get(macro)
        if role is None:
            raise ContractError(f"{field}.macro: unknown route role {macro!r}")
        if role["function"] != function or role["peripheral"] != peripheral:
            raise ContractError(
                f"{field}: route role {macro!r} requires function "
                f"{role['function']!r} on {role['peripheral']!r}"
            )
        if route_id in ids:
            raise ContractError(f"{field}.id: duplicate {route_id!r}")
        previous_macro = macro_routes.get(macro)
        if previous_macro is not None:
            same_exclusive_group = (
                route.get("exclusive_group") is not None
                and route.get("exclusive_group") == previous_macro.get("exclusive_group")
            )
            if not same_exclusive_group:
                raise ContractError(f"{field}.macro: duplicate {macro!r}")
        ids.add(route_id)
        macro_routes[macro] = route
        if "compile" in route:
            _boolean(route["compile"], f"{field}.compile")
        if "exclusive_group" in route:
            group = _string(route["exclusive_group"], f"{field}.exclusive_group", pattern=IDENTIFIER_RE)
            groups.setdefault(group, []).append(route)
        if "runtime_mux_group" in route:
            runtime_group = _string(
                route["runtime_mux_group"],
                f"{field}.runtime_mux_group",
                pattern=IDENTIFIER_RE,
            )
            if "exclusive_group" in route:
                raise ContractError(
                    f"{field}: exclusive_group and runtime_mux_group are mutually exclusive"
                )
            if not route.get("compile", True):
                raise ContractError(f"{field}: runtime mux routes must compile")
            runtime_mux_groups.setdefault(runtime_group, []).append(route)

    for left_index, left in enumerate(routes):
        for right in routes[left_index + 1:]:
            pin_collision = left["pin"] == right["pin"]
            function_collision = left["function"] == right["function"]
            same_exclusive_group = (
                left.get("exclusive_group") is not None
                and left.get("exclusive_group") == right.get("exclusive_group")
            )
            same_runtime_mux_group = (
                left.get("runtime_mux_group") is not None
                and left.get("runtime_mux_group") == right.get("runtime_mux_group")
            )
            allowed = same_exclusive_group or (
                pin_collision and not function_collision and same_runtime_mux_group
            )
            if (pin_collision or function_collision) and not allowed:
                what = "pin" if pin_collision else "function"
                raise ContractError(
                    f"routes: {left['id']!r} and {right['id']!r} duplicate {what} "
                    "outside one compatible route group"
                )

    for group, candidates in runtime_mux_groups.items():
        if data["support"] != "runtime":
            raise ContractError(
                f"runtime_mux_group {group!r}: only runtime ports may preserve a legacy mux"
            )
        if len(candidates) < 2:
            raise ContractError(f"runtime_mux_group {group!r}: requires multiple routes")
        pins = [route["pin"] for route in candidates]
        if len(pins) == len(set(pins)):
            raise ContractError(
                f"runtime_mux_group {group!r}: must describe an intentional pin collision"
            )
        if len({route["peripheral"] for route in candidates}) < 2:
            raise ContractError(
                f"runtime_mux_group {group!r}: must describe multiple peripheral modes"
            )
        expected = registry.data["legacy_runtime_muxes"].get(group)
        if expected is None:
            raise ContractError(
                f"runtime_mux_group {group!r}: not a registry-defined legacy exception"
            )
        actual_routes = {
            (route["macro"], route["function"], route["pin"], route["peripheral"])
            for route in candidates
        }
        expected_routes = {
            (route["macro"], route["function"], route["pin"], route["peripheral"])
            for route in expected["routes"]
        }
        if actual_routes != expected_routes or len(candidates) != len(expected_routes):
            raise ContractError(
                f"runtime_mux_group {group!r}: routes must exactly match the "
                "registry-defined preservation exception"
            )

    raw_selections = data.get("route_selections", {})
    if not isinstance(raw_selections, dict):
        raise ContractError("route_selections: expected table")
    unknown_groups = sorted(set(raw_selections) - set(groups))
    if unknown_groups:
        raise ContractError("route_selections: unknown group(s): " + ", ".join(unknown_groups))
    for group, selected in raw_selections.items():
        _string(selected, f"route_selections.{group}", pattern=IDENTIFIER_RE)
        candidates = {route["id"] for route in groups[group]}
        if selected not in candidates:
            raise ContractError(f"route_selections.{group}: route is not in this group")
        chosen = next(route for route in groups[group] if route["id"] == selected)
        if not chosen.get("compile", True):
            raise ContractError(f"route_selections.{group}: selected route has compile=false")
    for group, candidates in groups.items():
        if group in raw_selections:
            continue
        if data["support"] == "runtime":
            raise ContractError(f"route_selections.{group}: runtime board must select exactly one route")
        if any(route.get("compile", True) for route in candidates):
            raise ContractError(
                f"route_selections.{group}: conformance omission requires every route compile=false"
            )


def _validate_programming(data: dict[str, Any], registry: PlatformRegistry,
                          board_dir: Path | None) -> None:
    programming = data.get("programming")
    if not isinstance(programming, dict):
        raise ContractError("programming: expected table")
    _strict(programming, PROGRAMMING_FIELDS, "programming")
    supported = _boolean(programming.get("supported"), "programming.supported")
    reset = _string(programming.get("reset_profile"), "programming.reset_profile")
    if reset not in registry.values("reset_profiles"):
        raise ContractError(f"programming.reset_profile: unknown profile {reset!r}")
    usb = programming.get("usb_detection")
    if not isinstance(usb, dict):
        raise ContractError("programming.usb_detection: expected table")
    _strict(usb, USB_FIELDS, "programming.usb_detection")
    mode = _string(usb.get("mode"), "programming.usb_detection.mode")
    if mode not in registry.values("usb_detection_modes"):
        raise ContractError(f"programming.usb_detection.mode: unknown mode {mode!r}")
    if mode == "vid-any":
        vids = _string_list(usb.get("vids"), "programming.usb_detection.vids")
        if not vids:
            raise ContractError(
                "programming.usb_detection.vids: vid-any requires at least one VID"
            )
        if any(VID_RE.fullmatch(vid) is None for vid in vids):
            raise ContractError("programming.usb_detection.vids: expected uppercase four-digit hex VIDs")
    elif "vids" in usb:
        raise ContractError("programming.usb_detection.vids: only valid with mode=vid-any")

    hardware_defaults = {
        "reboot_profile", "isp_stub", "flash_type", "flash_mode",
        "boot_baud", "flash_baud", "reset_attempts",
    }
    if not supported:
        forbidden = sorted(hardware_defaults & set(programming))
        if forbidden:
            raise ContractError(
                "programming: supported=false forbids hardware defaults: " + ", ".join(forbidden)
            )
        if mode != "explicit-port":
            raise ContractError("programming.usb_detection.mode: unsupported ports require explicit-port")
        return
    missing = sorted(hardware_defaults - set(programming))
    if missing:
        raise ContractError("programming: missing required field(s): " + ", ".join(missing))
    reboot = _string(programming["reboot_profile"], "programming.reboot_profile")
    if reboot not in registry.values("reboot_profiles"):
        raise ContractError(f"programming.reboot_profile: unknown profile {reboot!r}")
    stub = _string(programming["isp_stub"], "programming.isp_stub")
    stub_path = Path(stub)
    if stub_path.is_absolute() or ".." in stub_path.parts:
        raise ContractError("programming.isp_stub: expected repository-relative safe path")
    if board_dir is not None:
        root = board_dir.parents[1]
        if not (root / stub_path).is_file():
            raise ContractError(f"programming.isp_stub: file does not exist: {stub}")
    if programming["flash_type"] not in registry.values("flash_types"):
        raise ContractError("programming.flash_type: unknown value")
    if programming["flash_mode"] not in registry.values("flash_modes"):
        raise ContractError("programming.flash_mode: unknown value")
    _integer(programming["boot_baud"], "programming.boot_baud", minimum=1)
    _integer(programming["flash_baud"], "programming.flash_baud", minimum=1)
    _integer(programming["reset_attempts"], "programming.reset_attempts", minimum=1, maximum=100)


def _validate_defaults_and_connectors(
    data: dict[str, Any], registry: PlatformRegistry
) -> None:
    defaults = data.get("defaults")
    if not isinstance(defaults, dict):
        raise ContractError("defaults: expected table")
    _strict(defaults, DEFAULT_FIELDS, "defaults")
    for name, value in defaults.items():
        minimum, maximum = DEFAULT_RANGES[name]
        _integer(value, f"defaults.{name}", minimum=minimum, maximum=maximum)
    for name, expected in K210_FIXED_DEFAULTS.items():
        if name in defaults and defaults[name] != expected:
            raise ContractError(f"defaults.{name}: K210 requires {expected}")
    if data["support"] == "runtime":
        missing_defaults = sorted(DEFAULT_FIELDS - set(defaults))
        if missing_defaults:
            raise ContractError(
                "defaults: runtime board is missing: " + ", ".join(missing_defaults)
            )
    routes = data["routes"]
    routes_by_id = {route["id"]: route for route in routes}
    routes_by_macro = {route["macro"]: route for route in routes}
    for field in ("lcd_spi", "sd_spi", "flash_spi"):
        if field in defaults:
            peripheral = f"spi{defaults[field]}"
            if peripheral not in registry.values("peripheral_instances"):
                raise ContractError(
                    f"defaults.{field}: unknown K210 peripheral {peripheral!r}"
                )
    for field, macro in GPIOHS_DEFAULT_ROUTES.items():
        if field not in defaults:
            continue
        route = routes_by_macro.get(macro)
        if route is None:
            raise ContractError(f"defaults.{field}: route {macro} is missing")
        match = re.fullmatch(r"gpiohs(\d+)", route["function"])
        if match is None or defaults[field] != int(match.group(1)):
            raise ContractError(
                f"defaults.{field}: must match {macro} function {route['function']!r}"
            )
    for field, macro in SPI_DEFAULT_ROUTES.items():
        if field not in defaults:
            continue
        route = routes_by_macro.get(macro)
        match = re.fullmatch(r"spi(\d+)", route["peripheral"] if route else "")
        if match is None or defaults[field] != int(match.group(1)):
            raise ContractError(
                f"defaults.{field}: must match {macro} peripheral"
            )
    for field, macro in SPI_CHIP_SELECT_DEFAULT_ROUTES.items():
        if field not in defaults:
            continue
        route = routes_by_macro.get(macro)
        match = re.fullmatch(r"spi\d+-ss(\d+)", route["function"] if route else "")
        if match is None or defaults[field] != int(match.group(1)):
            raise ContractError(
                f"defaults.{field}: must match {macro} chip-select function"
            )
    for device_field, channel_field, macro in PWM_DEFAULT_ROUTES:
        if device_field not in defaults or channel_field not in defaults:
            continue
        route = routes_by_macro.get(macro)
        match = re.fullmatch(r"timer(\d+)-toggle(\d+)", route["function"] if route else "")
        if match is None:
            raise ContractError(
                f"defaults.{device_field}/{channel_field}: {macro} is not a PWM timer route"
            )
        expected_device = int(match.group(1))
        expected_channel = int(match.group(2)) - 1
        if (defaults[device_field] != expected_device or
                defaults[channel_field] != expected_channel):
            raise ContractError(
                f"defaults.{device_field}/{channel_field}: must match {macro} "
                f"timer{expected_device} channel {expected_channel}"
            )

    connectors = data.get("connectors", [])
    if not isinstance(connectors, list):
        raise ContractError("connectors: expected array of tables")
    connector_ids: set[str] = set()
    claimed_routes: set[str] = set()
    for index, connector in enumerate(connectors):
        field = f"connectors[{index}]"
        if not isinstance(connector, dict):
            raise ContractError(f"{field}: expected table")
        _strict(connector, CONNECTOR_FIELDS, field)
        connector_id = _string(connector.get("id"), f"{field}.id", pattern=IDENTIFIER_RE)
        if connector_id in connector_ids:
            raise ContractError(f"{field}.id: duplicate {connector_id!r}")
        connector_ids.add(connector_id)
        _string(connector.get("kind"), f"{field}.kind", pattern=IDENTIFIER_RE)
        pins = connector.get("pins")
        if not isinstance(pins, list):
            raise ContractError(f"{field}.pins: expected integer array")
        for pin_index, pin in enumerate(pins):
            _integer(pin, f"{field}.pins[{pin_index}]", maximum=47)
        if len(pins) != len(set(pins)):
            raise ContractError(f"{field}.pins: duplicate pin")
        protocols = _string_list(connector.get("protocols"), f"{field}.protocols")
        route_ids = _string_list(connector.get("routes"), f"{field}.routes")
        unknown_routes = sorted(set(route_ids) - set(routes_by_id))
        if unknown_routes:
            raise ContractError(f"{field}.routes: unknown route(s): {', '.join(unknown_routes)}")
        duplicate_claims = sorted(set(route_ids) & claimed_routes)
        if duplicate_claims:
            raise ContractError(
                f"{field}.routes: already assigned to another connector: "
                + ", ".join(duplicate_claims)
            )
        claimed_routes.update(route_ids)
        if route_ids:
            expected_pins = sorted({routes_by_id[route_id]["pin"] for route_id in route_ids})
            if sorted(pins) != expected_pins:
                raise ContractError(
                    f"{field}.pins: must exactly match referenced route pins {expected_pins}"
                )
            expected_protocols = {
                routes_by_id[route_id]["peripheral"] for route_id in route_ids
            }
            if set(protocols) != expected_protocols:
                raise ContractError(
                    f"{field}.protocols: must exactly match route peripherals "
                    + ", ".join(sorted(expected_protocols))
                )
        elif protocols:
            raise ContractError(
                f"{field}.protocols: protocol semantics require explicit routes"
            )
        elif pins and data.get("support") != "conformance":
            raise ContractError(
                f"{field}.pins: physical-only connector inventory is conformance-only"
            )


def validate_board_data(data: dict[str, Any], registry: PlatformRegistry,
                        *, board_dir: Path | None = None) -> None:
    _strict(data, ROOT_FIELDS, "board")
    if type(data.get("schema")) is not int or data["schema"] != 1:
        raise ContractError("schema: expected 1")
    board_id = _string(data.get("id"), "id", pattern=BOARD_ID_RE)
    if board_dir is not None and board_dir.name != board_id:
        raise ContractError(f"id: {board_id!r} does not match directory {board_dir.name!r}")
    if data.get("platform") != registry.platform:
        raise ContractError(f"platform: expected {registry.platform!r}")
    support = data.get("support")
    if support not in {"runtime", "conformance"}:
        raise ContractError("support: expected runtime or conformance")
    releaseable = _boolean(data.get("releaseable"), "releaseable")
    profile = data.get("runtime_profile")
    profiles = registry.data["runtime_profiles"]
    if support == "runtime":
        if not isinstance(profile, str) or profile not in profiles:
            raise ContractError("runtime_profile: runtime board requires a known profile")
    elif profile is not None:
        raise ContractError("runtime_profile: forbidden for conformance board")
    if releaseable and support != "runtime":
        raise ContractError("releaseable: true requires support=runtime")

    _validate_devices(data, registry)
    if support == "runtime":
        supported_kinds = {
            item["kind"] for item in data["devices"] if item["support"] == "driver"
        }
        required = set(profiles[profile]["required_devices"])
        missing = sorted(required - supported_kinds)
        if missing:
            raise ContractError(
                f"runtime_profile {profile!r}: missing driver-supported device(s): " + ", ".join(missing)
            )
    _validate_routes(data, registry)

    _validate_defaults_and_connectors(data, registry)
    _validate_programming(data, registry, board_dir)


def board_ids(root: Path = ROOT) -> list[str]:
    boards = root / "boards"
    if not boards.is_dir():
        return []
    return sorted(
        path.name for path in boards.iterdir()
        if path.is_dir() and (path / "board.toml").is_file()
    )


def load_board(board_id: str, *, root: Path = ROOT,
               registry: PlatformRegistry | None = None) -> Board:
    if BOARD_ID_RE.fullmatch(board_id) is None:
        raise ContractError(f"board: invalid canonical ID {board_id!r}")
    directory = root / "boards" / board_id
    path = directory / "board.toml"
    if not path.is_file():
        known = ", ".join(board_ids(root)) or "none"
        raise ContractError(f"board: unknown ID {board_id!r}; known boards: {known}")
    registry = registry or load_registry(root / "platforms" / "k210" / "devices.toml")
    data = _load_toml(path)
    validate_board_data(data, registry, board_dir=directory)
    for required in (directory / "board.c", directory / "flash_layout.json"):
        if not required.is_file():
            raise ContractError(f"board {board_id!r}: required file missing: {required.name}")
    return Board(path=path, data=data, registry=registry)


def _strip_c_comments(source: str) -> str:
    """Remove C comments while preserving strings and character literals."""
    result: list[str] = []
    index = 0
    state = "code"
    while index < len(source):
        current = source[index]
        following = source[index + 1] if index + 1 < len(source) else ""
        if state == "line-comment":
            if current == "\n":
                result.append(current)
                state = "code"
            else:
                result.append(" ")
            index += 1
            continue
        if state == "block-comment":
            if current == "*" and following == "/":
                result.extend((" ", " "))
                index += 2
                state = "code"
            else:
                result.append("\n" if current == "\n" else " ")
                index += 1
            continue
        if state in {"string", "character"}:
            result.append(current)
            if current == "\\" and following:
                result.append(following)
                index += 2
                continue
            terminator = '"' if state == "string" else "'"
            if current == terminator:
                state = "code"
            index += 1
            continue
        if current == "/" and following == "/":
            result.extend((" ", " "))
            index += 2
            state = "line-comment"
        elif current == "/" and following == "*":
            result.extend((" ", " "))
            index += 2
            state = "block-comment"
        else:
            result.append(current)
            if current == '"':
                state = "string"
            elif current == "'":
                state = "character"
            index += 1
    return "".join(result)


def validate_board_source(board: Board) -> None:
    source_path = board.directory / "board.c"
    source = _strip_c_comments(source_path.read_text(encoding="utf-8"))
    match = re.search(
        r"const\s+hk_board_ops_t\s+hk_board_ops\s*=\s*\{(?P<body>.*?)\}\s*;",
        source,
        re.DOTALL,
    )
    if match is None:
        raise ContractError(f"{source_path}: missing const hk_board_ops_t hk_board_ops initializer")
    body = match.group("body")
    callbacks = {
        name: value.strip()
        for name, value in re.findall(r"\.([a-z0-9_]+)\s*=\s*([^,}]+)", body)
    }
    required = {"early_init"} | board.required_callbacks()
    missing = sorted(name for name in required if callbacks.get(name) in {None, "NULL", "0"})
    if missing:
        raise ContractError(
            f"{source_path}: required non-NULL board callback(s): " + ", ".join(missing)
        )


def load_all_boards(root: Path = ROOT) -> list[Board]:
    registry = load_registry(root / "platforms" / "k210" / "devices.toml")
    result = [load_board(board_id, root=root, registry=registry) for board_id in board_ids(root)]
    for board in result:
        validate_board_source(board)
    return result
