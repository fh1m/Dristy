#!/usr/bin/env python3
"""Generate and stale-check private headers for one selected board port."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys

from board_contract import Board, ContractError, load_board, validate_board_source
from gen_flash_layout import load_validated, render as render_flash_layout


ROOT = Path(__file__).resolve().parents[1]

DEFAULT_MACROS = {
    "lcd_width": "LCD_W",
    "lcd_height": "LCD_H",
    "lcd_spi": "LCD_SPI",
    "lcd_chip_select": "LCD_CS",
    "lcd_spi_hz": "LCD_SPI_HZ",
    "gpiohs_lcd_reset": "GPIOHS_LCD_RST",
    "gpiohs_lcd_dc": "GPIOHS_LCD_DC_OR_AUX",
    "gpiohs_button_left": "GPIOHS_BTN_LEFT",
    "gpiohs_button_ok": "GPIOHS_BTN_OK",
    "gpiohs_button_right": "GPIOHS_BTN_RIGHT",
    "gpiohs_button_back": "GPIOHS_BTN_BACK",
    "gpiohs_sd_chip_select": "GPIOHS_SD_CS",
    "sd_spi": "SD_SPI",
    "sd_chip_select": "SD_SPI_CS",
    "flash_spi": "FLASH_SPI",
    "flash_chip_select": "FLASH_SPI_CS",
    "led_pwm_device": "LED_PWM_DEVICE",
    "led_pwm_channel": "LED_PWM_CHANNEL",
    "rgb_pwm_device": "RGB_PWM_DEVICE",
    "rgb_pwm_channel0": "RGB_PWM_CHANNEL0",
    "rgb_pwm_channel1": "RGB_PWM_CHANNEL1",
    "rgb_pwm_channel2": "RGB_PWM_CHANNEL2",
    "screen_pwm_device": "SCREEN_BL_PWM_DEVICE",
    "screen_pwm_channel": "SCREEN_BL_PWM_CHANNEL",
    "pwm_frequency_hz": "PWM_FREQ_HZ",
    "camera_sccb_address": "CAMERA_SCCB_ADDR",
    "camera_xclk_hz": "CAMERA_XCLK_HZ",
    "camera_sccb_hz": "CAMERA_SCCB_HZ",
    "camera_max_width": "CAMERA_MAX_W",
    "camera_max_height": "CAMERA_MAX_H",
}


def _guard(name: str) -> str:
    return "HK_GENERATED_" + re.sub(r"[^A-Z0-9]", "_", name.upper())


def _macro(name: str) -> str:
    return re.sub(r"[^A-Z0-9]", "_", name.upper())


def render_pins(board: Board) -> str:
    guard = _guard("pins_h")
    lines = [
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        f"/* Generated from boards/{board.id}/board.toml. Do not edit. */",
        "",
    ]
    for route in board.selected_routes():
        lines.append(f"#define {route['macro']} {route['pin']}")
        lines.append(f'#define {route["macro"]}_LABEL "IO{route["pin"]}"')
        function_macro = board.registry.function_macro(route["function"])
        lines.append(f"#define {route['macro']}_FUNCTION {function_macro}")
    lines.extend(["", "#endif", ""])
    return "\n".join(lines)


def render_defaults(board: Board) -> str:
    guard = _guard("defaults_h")
    lines = [
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        f"/* Generated from boards/{board.id}/board.toml. Do not edit. */",
        "",
    ]
    for field, macro in DEFAULT_MACROS.items():
        if field not in board.data["defaults"]:
            continue
        value = board.data["defaults"][field]
        suffix = (
            ".0" if field == "pwm_frequency_hz"
            else "" if field in {"lcd_width", "lcd_height"}
            else "U"
        )
        lines.append(f"#define {macro} {value}{suffix}")
        lines.append(f'#define {macro}_LABEL "{value}"')
    defaults = board.data["defaults"]
    if "gpiohs_lcd_dc" in defaults:
        lines.append("#define GPIOHS_LCD_DC_BIT (1U << GPIOHS_LCD_DC_OR_AUX)")
    if "camera_max_width" in defaults and "camera_max_height" in defaults:
        lines.append("#define CAMERA_MAX_FRAME_PIXELS (CAMERA_MAX_W * CAMERA_MAX_H)")
    lines.extend(["", "#endif", ""])
    return "\n".join(lines)


def render_inventory(board: Board) -> str:
    guard = _guard("inventory_h")
    profile = board.runtime_profile or ""
    lines = [
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        f"/* Generated from boards/{board.id}/board.toml. Private build metadata. */",
        f'#define HK_BOARD_ID "{board.id}"',
        f'#define HK_BOARD_PLATFORM "{board.registry.platform}"',
        f'#define HK_BOARD_SUPPORT "{board.support}"',
        f"#define HK_BOARD_RELEASEABLE {1 if board.releaseable else 0}U",
        f'#define HK_BOARD_RUNTIME_PROFILE "{profile}"',
        f"#define HK_BOARD_SELECTED_ROUTE_COUNT {len(board.selected_routes())}U",
        "",
    ]
    supported = board.driver_supported_kinds()
    for kind in board.registry.device_kinds():
        present = any(device["kind"] == kind for device in board.data["devices"])
        token = _macro(kind)
        lines.append(f"#define HK_BOARD_HAS_{token} {1 if present else 0}U")
        lines.append(f"#define HK_BOARD_DRIVER_{token} {1 if kind in supported else 0}U")
    lines.extend(["", "#endif", ""])
    return "\n".join(lines)


def generated_files(board: Board) -> dict[str, str]:
    flash, partitions = load_validated(board.flash_layout_path)
    return {
        "pins.h": render_pins(board),
        "defaults.h": render_defaults(board),
        "inventory.h": render_inventory(board),
        "flash_layout.h": render_flash_layout(
            board.flash_layout_path.relative_to(ROOT), flash, partitions
        ),
    }


def generate(board: Board, *, check: bool) -> list[str]:
    validate_board_source(board)
    expected = generated_files(board)
    failures: list[str] = []
    for name, content in expected.items():
        path = board.generated_dir / name
        if check:
            try:
                current = path.read_text(encoding="utf-8")
            except OSError:
                failures.append(f"missing generated file: {path}")
                continue
            if current != content:
                failures.append(f"stale generated file: {path}")
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8", newline="\n")
        print(f"generated {path}")
    return failures


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board", required=True, help="Canonical board.toml ID")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)
    try:
        board = load_board(args.board)
        failures = generate(board, check=args.check)
    except (ContractError, ValueError) as exc:
        print(f"board contract error: {exc}", file=sys.stderr)
        return 2
    if failures:
        for failure in failures:
            print(failure, file=sys.stderr)
        return 1
    if args.check:
        print(f"[OK] {board.id}: descriptor, BSP and generated headers are current")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
