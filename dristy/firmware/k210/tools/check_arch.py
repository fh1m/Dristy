#!/usr/bin/env python3
"""Check Dristy firmware layer and feature-module boundaries."""

from __future__ import annotations

import argparse
import ast
from dataclasses import dataclass
import fnmatch
import hashlib
import json
import os
import re
import shutil
import subprocess
try:
    import tomllib
except ModuleNotFoundError:  # Python 3.10 and older
    import tomli as tomllib
from pathlib import Path

import check_capabilities


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "firmware" / "src"
LAYER_POLICY_PATH = ROOT / "tools" / "architecture_layers.toml"
DIRECTIVE_RE = re.compile(
    r"^\s*(?:#|%:)\s*(?P<keyword>[A-Za-z_][A-Za-z0-9_]*)\b(?P<operand>.*)$"
)
INCLUDE_OPERAND_RE = re.compile(
    r'^\s*(?:<(?P<angle>[^>\r\n]+)>|"(?P<quote>[^"\r\n]+)")\s*$'
)
SDK_HEADER_FALLBACK = {
    "aes.h", "apu.h", "atomic.h", "bsp.h", "clint.h", "dmac.h",
    "dump.h", "dvp.h", "encoding.h", "entry.h", "fft.h", "fpioa.h",
    "gpio.h", "gpio_common.h", "gpiohs.h", "i2c.h", "i2s.h",
    "interrupt.h", "io.h", "iomem.h", "kpu.h", "platform.h", "plic.h",
    "printf.h", "pwm.h", "rtc.h", "sha256.h", "sleep.h", "spi.h",
    "syscalls.h", "sysctl.h", "timer.h", "uart.h", "uarths.h", "util.h",
    "utils.h", "wdt.h", "nncase.h", "nncase/runtime/k210/runtime_module.h",
    "syslog.h",
}
PRIVATE_BOARD_HEADERS = {
    "pins.h", "defaults.h", "inventory.h", "flash_layout.h",
    "hk_board_port.h",
}


def discover_sdk_headers() -> set[str]:
    """Discover every header exposed by the linked SDK's canonical include graph.

    The SDK's ``header_directories(${SDK_ROOT}/lib)`` recursively exposes each
    directory containing a header, including nncase and utils, not just BSP and
    drivers.  Header basenames and paths relative to each include directory are
    retained so both ``nncase.h`` and ``nncase/runtime/...`` spellings match.
    """

    result = set(SDK_HEADER_FALLBACK)
    sdk = ROOT / "_deps" / "kendryte-standalone-sdk"
    header_roots = [sdk / "include", sdk / "lib"]
    for root in header_roots:
        if not root.is_dir():
            continue
        headers = sorted(
            path for path in root.rglob("*")
            if path.is_file() and path.suffix.lower() in {".h", ".hpp"}
        )
        include_directories = {path.parent for path in headers}
        for path in headers:
            result.add(path.name.casefold())
            relative_parts = path.relative_to(root).parts
            for offset in range(len(relative_parts)):
                result.add(
                    "/".join(relative_parts[offset:]).casefold()
                )
            for include_directory in include_directories:
                try:
                    result.add(
                        path.relative_to(include_directory).as_posix().casefold()
                    )
                except ValueError:
                    pass
    return result


SDK_HEADERS = discover_sdk_headers()
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}
HARDWARE_LAYERS = {"board", "platform-hal", "driver"}
FORBIDDEN_DIRECT_SYMBOL_PREFIXES = (
    "hal_", "hk_board_", "dmac_", "dvp_", "fpioa_", "gpio_",
    "gpiohs_", "i2c_", "kpu_", "plic_", "pwm_", "sd_spi_", "spi_",
    "sysctl_", "timer_", "uart_", "uarths_", "wdt_",
)
SDK_TOKEN_RE = re.compile(
    r"\b(dmac_|dvp_|fpioa_|gpio_|gpiohs_|i2c_|plic_|kpu_|pwm_|spi_|"
    r"sysctl_|timer_|uart_|uarths_|wdt_|msleep|"
    r"sysctl_get_time_us|DVP_|FUNC_CMOS|FUNC_SCCB|FUNC_SPI|FPIOA_|"
    r"SPI_DEVICE|SPI_CHIP|SPI_WORK)"
)
TRIGRAPHS = {
    "??=": "#",
    "??/": "\\",
    "??'": "^",
    "??(": "[",
    "??)": "]",
    "??!": "|",
    "??<": "{",
    "??>": "}",
    "??-": "~",
}
PUBLIC_MUTABLE_EXTERN_RE = re.compile(
    r"^\s*extern\s+(?!const\b)(?!.*\()\S.*\s+\**[A-Za-z_]\w*"
    r"(?:\s*\[[^\]]*\])?\s*;"
)

FEATURES = {
    "terminal": ("terminal", "terminal_app.h"),
    "camera": ("camera", "camera_app.h"),
    "qr-camera": ("qr_camera", "qr_camera_app.h"),
    "face-detect": ("face_detect", "face_detect_app.h"),
    "apriltag": ("apriltag", "apriltag_app.h"),
    "object-detect": ("object_detect", "object_detect_app.h"),
    "files": ("files", "files_app.h"),
    "buttons": ("buttons", "buttons_app.h"),
    "pong": ("pong", "pong_app.h"),
    "settings": ("settings", "settings_app.h"),
    "sleep": ("sleep", "sleep_app.h"),
    "micropython": ("micropython", "micropython_app.h"),
}
FEATURE_DIRS = {f"apps/{directory}/": (name, public)
                for name, (directory, public) in FEATURES.items()}
REGISTRY = "apps/app_registry.c"

LEGACY_PATHS = {
    "apps/app_terminal.c", "apps/app_terminal.h",
    "apps/app_camera.c", "apps/app_camera.h",
    "apps/app_qr_camera.c", "apps/app_qr_camera.h",
    "apps/app_face_detect.c", "apps/app_face_detect.h",
    "apps/app_files.c", "apps/app_files.h",
    "apps/app_buttons.c", "apps/app_buttons.h",
    "apps/app_pong.c", "apps/app_pong.h",
    "apps/app_settings.c", "apps/app_settings.h",
    "apps/app_sleep.c", "apps/app_sleep.h",
    "controllers/buttons_controller.c", "controllers/buttons_controller.h",
    "controllers/camera_photo_controller.c", "controllers/camera_photo_controller.h",
    "controllers/camera_photo_mode_controller.c", "controllers/camera_photo_mode_controller.h",
    "controllers/camera_settings_controller.c", "controllers/camera_settings_controller.h",
    "controllers/camera_settings_coordinator.c", "controllers/camera_settings_coordinator.h",
    "controllers/camera_settings_menu.c", "controllers/camera_settings_menu.h",
    "controllers/files_actions.c", "controllers/files_actions.h",
    "controllers/files_backend.c", "controllers/files_controller.c",
    "controllers/files_controller.h", "controllers/files_presenter.c",
    "controllers/files_presenter.h",
    "controllers/qr_camera_mode_controller.c", "controllers/qr_camera_mode_controller.h",
    "controllers/qr_result_controller.c", "controllers/qr_result_controller.h",
    "controllers/settings_app_menu.c", "controllers/settings_app_menu.h",
    "controllers/settings_controller.c", "controllers/settings_controller.h",
    "controllers/sleep_controller.c", "controllers/sleep_controller.h",
    "controllers/screen_controller.c", "controllers/screen_controller.h",
    "services/photo_service.c", "services/photo_service.h",
    "services/qr_camera_frame_adapter.c", "services/qr_camera_frame_adapter.h",
    "services/qr_decoder_engine.c", "services/qr_decoder_engine.h",
    "services/qr_luma.c", "services/qr_luma.h",
    "services/qr_result.h", "services/qr_result_state.c",
    "services/qr_service.c", "services/qr_service.h",
    "storage/file_browser_state.c", "storage/file_delete.c", "storage/file_delete.h",
    "storage/file_dir.c", "storage/file_dir.h",
    "storage/file_preview.c", "storage/file_preview.h",
    "storage/image_decode.h", "storage/image_decode_bmp.c",
    "storage/image_decode_common.c", "storage/image_decode_gif.c",
    "storage/image_decode_gif.h", "storage/image_decode_png.c",
    "storage/image_decode_png_inflate.c", "storage/image_decode_png_inflate.h",
    "storage/image_decode_ppm.c", "storage/image_decode_raw.c",
    "storage/image_viewer.c", "storage/image_viewer.h",
    "storage/photo_encode.c", "storage/photo_encode.h",
    "storage/photo_format.c", "storage/photo_format.h",
    "storage/photo_path.c", "storage/photo_path.h",
    "storage/photo_writer.c", "storage/photo_writer.h",
    "storage/qr_text_path.c", "storage/qr_text_path.h",
    "storage/qr_text_writer.c", "storage/qr_text_writer.h",
    "ui/buttons_view.c", "ui/buttons_view.h", "ui/files_view.c",
    "ui/qr_result_view.c", "ui/qr_result_view.h",
    "ui/sleep_view.c", "ui/sleep_view.h",
    "config/file_browser_config.h", "config/files_input_config.h",
    "config/files_layout.h", "config/image_decode_config.h",
    "config/photo_storage_config.h", "config/qr_layout.h",
    "core/files_view_port.h", "core/indexed_image.h",
}

FORBIDDEN_FILES = {
    "core/hk_settings.h", "core/hk_board_config.h", "core/hk_types.h",
    "services/camera_types.h", "services/camera_service.h",
    "services/camera_settings.h", "services/camera_input_state.h",
    "services/qr_result_state.h", "storage/hk_fat32.h",
    "services/camera_photo.h",
    "apps/face_detect/face_detect_model_storage.c",
    "apps/face_detect/face_detect_model_storage.h",
    "storage/file_types.h", "storage/file_view_bridge.h",
    "storage/file_browser_state.h", "storage/fat32_state_private.h",
    "services/settings_types.h", "apps/app_entrypoints.h",
    "config/settings_layout.h", "controllers/camera_settings_model.c",
    "controllers/camera_settings_model.h", "controllers/settings_actions.c",
    "controllers/settings_actions.h", "controllers/settings_model.c",
    "controllers/settings_model.h", "services/camera_settings_navigation.h",
    "ui/camera_settings_view.c", "ui/camera_settings_view.h",
    "ui/settings_view.c", "ui/settings_view.h",
}

SETTINGS_MENU_SHARED = {
    "config/settings_menu_layout.h",
    "controllers/settings_menu_controller.c",
    "controllers/settings_menu_controller.h",
    "ui/settings_menu_view.c",
    "ui/settings_menu_view.h",
}

AI_MODEL_SHARED = {
    "core/ai_model_types.h",
    "storage/ai_model_storage.c",
    "storage/ai_model_storage.h",
    "services/ai_model_runtime.c",
    "services/ai_model_runtime.h",
}


def relative(path: Path) -> str:
    return path.relative_to(SRC).as_posix()


def source_files() -> list[Path]:
    return sorted(
        path for path in SRC.rglob("*")
        if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES
    )


def load_layer_policy(path: Path = LAYER_POLICY_PATH) -> dict[str, object]:
    """Load and strictly validate the explicit Phase 2 layer map."""

    document = tomllib.loads(path.read_text(encoding="utf-8"))
    if set(document) != {"schema", "source_roots", "layers", "forbidden_edges"}:
        raise RuntimeError("architecture layer policy has missing or unknown fields")
    if document["schema"] != 1:
        raise RuntimeError("architecture layer policy schema must be 1")
    roots = document["source_roots"]
    layers = document["layers"]
    edges = document["forbidden_edges"]
    if not isinstance(roots, list) or not roots or any(
            not isinstance(item, str) or not item for item in roots):
        raise RuntimeError("architecture source_roots must be non-empty strings")
    names: set[str] = set()
    for layer in layers:
        if set(layer) != {"name", "paths"} or not isinstance(layer["name"], str):
            raise RuntimeError("architecture layer entries require name and paths")
        if layer["name"] in names:
            raise RuntimeError(f"duplicate architecture layer {layer['name']!r}")
        names.add(layer["name"])
        if not isinstance(layer["paths"], list) or not layer["paths"]:
            raise RuntimeError(f"architecture layer {layer['name']!r} has no paths")
    for edge in edges:
        if set(edge) != {"from", "to", "reason"}:
            raise RuntimeError("forbidden edge entries require from, to, and reason")
        if (not set(edge["from"]).issubset(names) or
                not set(edge["to"]).issubset(names)):
            raise RuntimeError("forbidden edge references an unknown layer")
        if not isinstance(edge["reason"], str) or not edge["reason"]:
            raise RuntimeError("forbidden edge reason must be non-empty")
    return document


def canonical_repository_path(path: str | Path) -> str:
    value = str(path).replace("\\", "/")
    while "//" in value:
        value = value.replace("//", "/")
    if value.startswith("./"):
        value = value[2:]
    return value


def classify_repository_path(
    path: str | Path, policy: dict[str, object] | None = None
) -> str | None:
    """Classify by repository-root path, never by a misleading basename."""

    policy = policy or load_layer_policy()
    normalized = canonical_repository_path(path).casefold().lstrip("/")
    matches: list[str] = []
    for layer in policy["layers"]:
        if any(fnmatch.fnmatchcase(normalized, pattern.casefold())
               for pattern in layer["paths"]):
            matches.append(layer["name"])
    if len(matches) > 1:
        raise RuntimeError(f"{path}: matches multiple architecture layers: {matches}")
    return matches[0] if matches else None


def layer_edge_violation(
    source: str | Path, target: str | Path,
    policy: dict[str, object] | None = None,
) -> str | None:
    policy = policy or load_layer_policy()
    source_layer = classify_repository_path(source, policy)
    target_layer = classify_repository_path(target, policy)
    if source_layer is None or target_layer is None:
        return None
    return repository_layer_violation(source_layer, target_layer, policy)


def repository_layer_violation(
    source_layer: str, target_layer: str,
    policy: dict[str, object] | None = None,
) -> str | None:
    """Apply the declarative policy to two already-classified layers."""

    policy = policy or load_layer_policy()
    for edge in policy["forbidden_edges"]:
        if source_layer in edge["from"] and target_layer in edge["to"]:
            return f"{edge['reason']} ({source_layer} -> {target_layer})"
    return None


def repository_source_files(
    policy: dict[str, object] | None = None,
) -> list[Path]:
    policy = policy or load_layer_policy()
    result: set[Path] = set()
    for root_name in policy["source_roots"]:
        root = ROOT / root_name
        if not root.is_dir():
            continue
        result.update(
            path for path in root.rglob("*")
            if path.is_file() and path.suffix.casefold() in SOURCE_SUFFIXES
        )
    return sorted(result)


def repository_relative(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def resolve_repository_include(
    path: Path, include: str, *, repository_root: Path = ROOT,
    source_root: Path = SRC,
) -> str | None:
    """Resolve repository includes to their real target, including symlinks."""

    normalized = normalize_include_path(include)
    search = (
        path.parent / normalized,
        repository_root / normalized,
        source_root / normalized,
        repository_root / "firmware" / "include" / normalized,
    )
    root = repository_root.resolve()
    for candidate in search:
        try:
            resolved = candidate.resolve()
            resolved.relative_to(root)
        except (OSError, ValueError):
            continue
        if resolved.is_file():
            return resolved.relative_to(root).as_posix()
    return None


def unresolved_hardware_violation(
    source_layer: str | None, include: str,
) -> str | None:
    if source_layer not in {"app", "adapter"}:
        return None
    policy_include = normalize_include_path(include).casefold()
    include_name = policy_include.rsplit("/", 1)[-1]
    if policy_include in SDK_HEADERS or include_name in SDK_HEADERS:
        return f"{source_layer} must not include K210 SDK headers"
    if ("platforms/" in policy_include or "boards/" in policy_include or
            include_name in PRIVATE_BOARD_HEADERS or
            re.fullmatch(r"hal_[a-z0-9_]+\.h", include_name)):
        return f"{source_layer} must not include board/BSP or platform HAL"
    if "/drivers/" in f"/{policy_include}" or policy_include.startswith("drivers/"):
        return f"{source_layer} must not include drivers"
    return None


def transitive_layer_failures(
    graph: dict[str, list[str]], policy: dict[str, object] | None = None,
) -> list[str]:
    """Carry the originating app/adapter policy through forwarding headers."""

    policy = policy or load_layer_policy()
    failures: list[str] = []
    for origin in sorted(graph):
        if classify_repository_path(origin, policy) not in {"app", "adapter"}:
            continue
        pending = list(graph[origin])
        visited: set[str] = set()
        while pending:
            target = pending.pop()
            if target in visited:
                continue
            visited.add(target)
            if violation := layer_edge_violation(origin, target, policy):
                failures.append(
                    f"{origin}: transitive dependency on {target}: {violation}"
                )
                continue
            pending.extend(graph.get(target, ()))
    return sorted(set(failures))


def _translation_phase_lines(
    source: str, *, preserve_literals: bool = True
) -> list[tuple[int, str]]:
    """Normalize directive-relevant C translation phases with source lines.

    Backslash-newline splicing happens before comment removal.  We also
    recognize digraph directives and C trigraphs so an app cannot spell a
    hidden include directive or SDK token around the guard.  String and
    character literal contents are retained only when requested by a caller.
    """

    source = source.replace("\r\n", "\n").replace("\r", "\n")
    spliced: list[str] = []
    line_map: list[int] = []
    index = 0
    line = 1
    while index < len(source):
        trigraph = TRIGRAPHS.get(source[index:index + 3])
        current = trigraph if trigraph is not None else source[index]
        consumed = 3 if trigraph is not None else 1
        if current == "\\":
            newline = index + consumed
            # GCC accepts horizontal whitespace before a spliced newline as an
            # extension.  Treat it conservatively as a splice too.
            while newline < len(source) and source[newline] in " \t\f\v":
                newline += 1
            if newline < len(source) and source[newline] == "\n":
                index = newline + 1
                line += 1
                continue
        spliced.append(current)
        line_map.append(line)
        if current == "\n":
            line += 1
        index += consumed

    text = "".join(spliced)
    visible = list(text)
    state = "code"
    index = 0
    while index < len(text):
        current = text[index]
        following = text[index + 1] if index + 1 < len(text) else ""
        if state == "line-comment":
            if current == "\n":
                state = "code"
            else:
                visible[index] = " "
            index += 1
            continue
        if state == "block-comment":
            if current == "*" and following == "/":
                visible[index] = visible[index + 1] = " "
                index += 2
                state = "code"
            else:
                if current != "\n":
                    visible[index] = " "
                index += 1
            continue
        if state in {"string", "character"}:
            if current == "\\" and following:
                if not preserve_literals:
                    visible[index] = " "
                    if following != "\n":
                        visible[index + 1] = " "
                index += 2
                continue
            terminator = '"' if state == "string" else "'"
            if not preserve_literals and current != "\n":
                visible[index] = " "
            if current == terminator:
                state = "code"
            index += 1
            continue
        if current == "/" and following == "/":
            visible[index] = visible[index + 1] = " "
            index += 2
            state = "line-comment"
        elif current == "/" and following == "*":
            visible[index] = visible[index + 1] = " "
            index += 2
            state = "block-comment"
        elif current == '"':
            state = "string"
            if not preserve_literals:
                visible[index] = " "
            index += 1
        elif current == "'":
            state = "character"
            if not preserve_literals:
                visible[index] = " "
            index += 1
        else:
            index += 1

    translated = "".join(visible)
    result: list[tuple[int, str]] = []
    offset = 0
    for logical in translated.splitlines(keepends=True):
        content = logical.rstrip("\n")
        mapped = line_map[offset:offset + len(logical)]
        number = mapped[0] if mapped else 1
        result.append((number, content))
        offset += len(logical)
    if translated and not translated.endswith("\n") and not result:
        result.append((line_map[0], translated))
    return result


def _include_directives(source: str) -> list[tuple[int, str, str | None, bool]]:
    result: list[tuple[int, str, str | None, bool]] = []
    for number, line in _translation_phase_lines(source):
        directive = DIRECTIVE_RE.match(line)
        if not directive:
            continue
        keyword = directive.group("keyword")
        if keyword not in {"include", "include_next", "import"}:
            continue
        operand = INCLUDE_OPERAND_RE.fullmatch(directive.group("operand"))
        include = None if operand is None else (
            operand.group("angle") or operand.group("quote")
        )
        raw_lines = source.replace("\r\n", "\n").replace("\r", "\n").splitlines()
        raw = raw_lines[number - 1] if 0 < number <= len(raw_lines) else ""
        ordinary = bool(
            re.fullmatch(
                r'\s*#\s*include\s*(?:<[^>\r\n]+>|"[^"\r\n]+")\s*',
                raw,
            )
        )
        result.append((number, keyword, include, ordinary))
    return result


def includes(path: Path) -> list[tuple[int, str]]:
    return [
        (number, include)
        for number, keyword, include, _ordinary in _include_directives(
            path.read_text(encoding="utf-8")
        )
        if keyword == "include" and include is not None
    ]


def nonliteral_include_lines(source: str) -> list[int]:
    """Allow only ordinary literal ``#include`` directives in apps."""

    return [
        number
        for number, keyword, include, ordinary in _include_directives(source)
        if keyword != "include" or include is None or not ordinary
    ]


def sdk_token_lines(source: str) -> list[tuple[int, str]]:
    """Find SDK tokens after C splicing, outside comments and literals."""

    result: list[tuple[int, str]] = []
    for number, line in _translation_phase_lines(
        source, preserve_literals=False
    ):
        directive = DIRECTIVE_RE.match(line)
        if directive and directive.group("keyword") in {
            "include", "include_next", "import"
        }:
            continue
        if match := SDK_TOKEN_RE.search(line):
            result.append((number, match.group(0)))
    return result


def normalize_include_path(include: str) -> str:
    """Use one policy separator regardless of host or source spelling."""

    return include.replace("\\", "/")


def resolve_include(path: Path, include: str) -> str | None:
    normalized = normalize_include_path(include)
    for candidate in (path.parent / normalized, SRC / normalized):
        try:
            resolved = candidate.resolve()
            resolved.relative_to(SRC.resolve())
        except ValueError:
            continue
        if resolved.is_file():
            return resolved.relative_to(SRC.resolve()).as_posix()
    return None


def feature_for(path: str) -> tuple[str, str, str] | None:
    for prefix, (name, public) in FEATURE_DIRS.items():
        if path.startswith(prefix):
            return prefix, name, public
    return None


def layer_violation(path: str, include: str, target: str | None) -> str | None:
    policy_include = normalize_include_path(include).casefold()
    include_name = policy_include.rsplit("/", 1)[-1]
    if (path.startswith("apps/") and target is None and
            (policy_include in SDK_HEADERS or include_name in SDK_HEADERS)):
        return "apps must not include K210 SDK headers"
    if path.startswith("apps/") and (
            "platforms/" in policy_include or "boards/" in policy_include or
            include_name in PRIVATE_BOARD_HEADERS or
            re.fullmatch(r"hal_[a-z0-9_]+\.h", include_name)):
        return "apps must use private runtime facades, not board/BSP or platform HAL"
    if (path.startswith("apps/") and target == "internal/time_internal.h"):
        return "apps must use the public Time capability"
    if path.startswith("apps/") and target and target.startswith("drivers/"):
        return "apps must use public capabilities or portable services, not drivers"
    if path.startswith("apps/") and target and target.startswith("capabilities/"):
        return "apps must not include capability implementation/private headers"
    if not path.startswith("runtime/") and target and target.startswith("runtime/"):
        return "only runtime may include runtime"
    if path.startswith("drivers/") and target and target.startswith(
            ("ui/", "apps/", "services/internal/", "storage/internal/")):
        return "drivers must not depend on UI, apps, or private state"
    if path.startswith("core/") and target and target.startswith(
            ("services/", "storage/", "ui/", "apps/", "controllers/")):
        return "core must not depend on upper layers"
    if path.startswith("storage/") and target and target.startswith(
            ("ui/", "apps/", "services/")):
        return "storage must not depend on UI, apps, or services"
    if path.startswith("services/") and target and target.startswith(
            ("ui/", "apps/")):
        return "services must not depend on UI or apps"
    if path.startswith("controllers/") and target and target.startswith(
            ("apps/", "services/internal/", "storage/internal/")):
        return "shared controllers must not depend on apps or private state"
    if path.startswith("ui/") and target and target.startswith(
            ("storage/", "services/")):
        return "shared UI must not depend on storage or services"
    if (path.startswith(("services/", "controllers/")) and
            path != "services/debug_console_service.c" and
            include_name == "hal_uart.h"):
        return "use the debug console service instead of debug UART"
    return None


def feature_include_violation(path: str, target: str | None) -> str | None:
    if not target:
        return None
    source_feature = feature_for(path)
    target_feature = feature_for(target)
    if not target_feature:
        return None
    target_prefix, target_name, target_public = target_feature
    if source_feature and source_feature[0] == target_prefix:
        return None
    if path == REGISTRY and target == f"{target_prefix}{target_public}":
        return None
    return f"only app registry may include the public {target_name} app header"


def settings_menu_violation(path: str, target: str | None) -> str | None:
    if target == "ui/settings_menu_view.h" and path not in {
            "controllers/settings_menu_controller.c", "ui/settings_menu_view.c"}:
        return "settings-menu view is private to its shared controller"
    if path not in SETTINGS_MENU_SHARED or not target:
        return None
    if target.startswith(("apps/", "services/", "storage/", "runtime/")) or "camera" in target:
        return "shared settings-menu must remain feature and service independent"
    return None


def ai_model_violation(path: str, target: str | None) -> str | None:
    if not target:
        return None
    if path.startswith("apps/") and target in {
            "storage/ai_model_storage.h", "hal/hal_kpu.h"}:
        return "feature apps must use the shared AI runtime, not its storage/HAL internals"
    if path not in AI_MODEL_SHARED:
        return None
    if target.startswith(("apps/", "ui/", "controllers/", "runtime/")) or "camera" in target:
        return "shared AI platform must remain feature and camera independent"
    return None


def include_cycle_failures() -> list[str]:
    graph: dict[str, list[str]] = {}
    for path in source_files():
        graph[relative(path)] = [
            target for _, include in includes(path)
            if (target := resolve_include(path, include))
        ]
    failures: list[str] = []
    visiting: set[str] = set()
    visited: set[str] = set()
    stack: list[str] = []

    def visit(node: str) -> None:
        if node in visiting:
            start = stack.index(node)
            failures.append("include cycle: " + " -> ".join(stack[start:] + [node]))
            return
        if node in visited:
            return
        visiting.add(node)
        stack.append(node)
        for child in graph.get(node, []):
            visit(child)
        stack.pop()
        visiting.remove(node)
        visited.add(node)

    for node in graph:
        visit(node)
    return failures


def layout_failures() -> list[str]:
    failures: list[str] = []
    manifest = (ROOT / "tools" / "build_firmware.py").read_text(encoding="utf-8")
    if any((SRC / name).is_dir() and any((SRC / name).iterdir())
           for name in ("board", "hal")):
        failures.append("firmware/src/board and firmware/src/hal must not exist")
    for required in (
        ROOT / "platforms" / "k210" / "hal",
        ROOT / "platforms" / "k210" / "startup",
        ROOT / "firmware" / "src" / "internal" / "boot_internal.h",
        ROOT / "firmware" / "include" / "dristy" / "capability" / "time.h",
        ROOT / "firmware" / "src" / "capabilities" / "time.c",
        ROOT / "platforms" / "k210" / "capabilities" / "time_adapter.c",
        ROOT / "firmware" / "include" / "dristy" / "capability" / "lights.h",
        ROOT / "firmware" / "src" / "capabilities" / "lights.c",
        ROOT / "platforms" / "k210" / "capabilities" / "lights_adapter.c",
        ROOT / "firmware" / "include" / "dristy" / "capability" / "display.h",
        ROOT / "docs" / "spec" / "capabilities" / "DISPLAY.md",
        ROOT / "tests" / "capability_fake_display.c",
        ROOT / "tests" / "display_contract_harness.c",
        ROOT / "tests" / "test_display_contract.py",
        ROOT / "firmware" / "app_requirements.toml",
        ROOT / "firmware" / "capability_consumers.toml",
        ROOT / "platforms" / "k210" / "capabilities.toml",
        ROOT / "tools" / "gen_capability_inventory.py",
        ROOT / "tools" / "check_capabilities.py",
        ROOT / "tools" / "architecture_layers.toml",
        ROOT / "tests" / "test_phase2_architecture.py",
        ROOT / "firmware" / "src" / "storage" / "sd_card.h",
        ROOT / "firmware" / "src" / "services" / "frame_pool.c",
        ROOT / "firmware" / "src" / "services" / "frame_pool.h",
        ROOT / "firmware" / "src" / "services" / "frame_workspace.h",
        ROOT / "firmware" / "src" / "core" / "hk_capability_client.h",
    ):
        if not required.exists():
            failures.append(
                f"{required.relative_to(ROOT).as_posix()}: required architecture path is missing"
            )
    for removed in (
        ROOT / "firmware" / "src" / "internal" / "time_internal.c",
        ROOT / "firmware" / "src" / "internal" / "time_internal.h",
        ROOT / "firmware" / "src" / "drivers" / "hk_sd.h",
        ROOT / "firmware" / "src" / "drivers" / "frame_pool.c",
        ROOT / "firmware" / "src" / "drivers" / "frame_pool.h",
        ROOT / "firmware" / "src" / "capabilities" / "capability_client_binding.h",
    ):
        if removed.exists():
            failures.append(
                f"{removed.relative_to(ROOT).as_posix()}: replaced private boundary must not exist"
            )
    for app_source in sorted((SRC / "apps").rglob("*")):
        if app_source.suffix not in {".c", ".h"}:
            continue
        app_text = app_source.read_text(encoding="utf-8")
        for forbidden_time_call in ("time_internal", "hal_time_us", "hal_sleep"):
            if forbidden_time_call in app_text:
                failures.append(
                    f"{relative(app_source)}: app time must use the public Time capability; "
                    f"found {forbidden_time_call}"
                )
    for consumer_root in (
        SRC / "apps", SRC / "controllers", SRC / "services", SRC / "runtime",
        SRC / "ui", ROOT / "platforms" / "k210" / "startup",
    ):
        for source in sorted(consumer_root.rglob("*")):
            if source.suffix not in {".c", ".h"}:
                continue
            text = source.read_text(encoding="utf-8")
            for forbidden_light_access in (
                "hk_lights.h", "lights_screen_backlight_set",
                "lights_screen_backlight_off", "lights_illum_set",
                "lights_rgb_set",
            ):
                if forbidden_light_access in text:
                    failures.append(
                        f"{relative(source)}: light consumers must use the public "
                        f"Lights capability; found {forbidden_light_access}"
                    )
    for path in sorted(LEGACY_PATHS | FORBIDDEN_FILES):
        if (SRC / path).exists():
            failures.append(f"{path}: legacy/forbidden path must not exist")
    for name, (directory, public) in FEATURES.items():
        module = SRC / "apps" / directory
        if not module.is_dir():
            failures.append(f"apps/{directory}: feature directory is missing")
        if not (module / public).is_file():
            failures.append(f"apps/{directory}/{public}: public app header is missing")
        marker = f'"{name}": Path("firmware/src/apps/{directory}")'
        if marker not in manifest:
            failures.append(f"tools/build_firmware.py: {name} is not directory-gated")
    for path in (SRC / "apps").glob("*.[ch]"):
        if path.name not in {"app_registry.c"}:
            failures.append(f"apps/{path.name}: flat app source is forbidden")
    if 'if "qr-camera" not in disabled_apps:' not in manifest:
        failures.append("tools/build_firmware.py: quirc is not gated by QR-CAMERA")
    if 'if "apriltag" not in disabled_apps:' not in manifest:
        failures.append("tools/build_firmware.py: AprilTag third party is not gated")
    for path in AI_MODEL_SHARED:
        if not (SRC / path).is_file():
            failures.append(f"{path}: shared AI platform file is missing")
    for path in (
        ROOT / "platforms" / "k210" / "hal" / "hal_kpu.c",
        ROOT / "platforms" / "k210" / "hal" / "hal_kpu.h",
    ):
        if not path.is_file():
            failures.append(f"{path.relative_to(ROOT).as_posix()}: shared AI HAL is missing")
    if "app_requirements.toml" in "\n".join(
            path.read_text(encoding="utf-8")
            for path in source_files()):
        failures.append("private build-time requirements must not provide runtime hardware access")
    for path in sorted((ROOT / "tools").glob("*.py")):
        for line, reason in board_behavior_violations(
            path.read_text(encoding="utf-8")
        ):
            failures.append(
                f"{path.relative_to(ROOT).as_posix()}:{line}: {reason}"
            )
    lock_path = ROOT / "models" / "toolchain.lock.json"
    if not lock_path.is_file() or '"sha256"' not in lock_path.read_text(encoding="utf-8"):
        failures.append("models/toolchain.lock.json: pinned compiler checksum is missing")
    return failures


def _board_selector(node: ast.AST) -> bool:
    for child in ast.walk(node):
        if isinstance(child, ast.Name) and child.id == "board_id":
            return True
        if isinstance(child, ast.Attribute) and (
            (isinstance(child.value, ast.Name)
             and child.value.id == "args" and child.attr == "board")
            or (isinstance(child.value, ast.Name)
                and child.value.id == "board" and child.attr == "id")
        ):
            return True
    return False


def _runtime_board_selector(node: ast.AST) -> bool:
    """Return true for the selected descriptor object / CLI identity."""

    return any(
        isinstance(child, ast.Attribute) and (
            (isinstance(child.value, ast.Name)
             and child.value.id == "args" and child.attr == "board")
            or (isinstance(child.value, ast.Name)
                and child.value.id == "board" and child.attr == "id")
        )
        for child in ast.walk(node)
    )


def _board_literal(node: ast.AST) -> bool:
    known = {
        path.name for path in (ROOT / "boards").iterdir()
        if path.is_dir()
    } if (ROOT / "boards").is_dir() else set()
    return any(
        isinstance(child, ast.Constant)
        and isinstance(child.value, str)
        and child.value in known
        for child in ast.walk(node)
    )


def board_behavior_violations(source: str) -> list[tuple[int, str]]:
    """Reject board-ID control flow/tables, while allowing identity metadata."""

    try:
        tree = ast.parse(source)
    except SyntaxError as exc:
        return [(exc.lineno or 1, "cannot parse Python for board behavior guard")]
    findings: list[tuple[int, str]] = []
    for node in ast.walk(tree):
        if isinstance(node, (ast.If, ast.IfExp, ast.While)):
            test = node.test
            comparisons = [
                item for item in ast.walk(test) if isinstance(item, ast.Compare)
            ]
            if any(
                _board_selector(compare)
                and (
                    _runtime_board_selector(compare)
                    or
                    _board_literal(compare)
                    or any(isinstance(operator, (ast.In, ast.NotIn))
                           for operator in compare.ops)
                )
                for compare in comparisons
            ):
                findings.append(
                    (node.lineno, "board-ID conditional is forbidden")
                )
        elif isinstance(node, ast.Match) and _board_selector(node.subject):
            findings.append((node.lineno, "board-ID match/case is forbidden"))
        elif isinstance(node, ast.Dict):
            if any(key is not None and _board_literal(key) for key in node.keys):
                findings.append(
                    (node.lineno, "board-ID behavior table is forbidden")
                )
    return sorted(set(findings))


def manual_provider_inventory_lines(source: str) -> list[int]:
    patterns = (
        re.compile(
            r"\bhk_capability_provider_t\s*\*\s*(?:const\s+)?"
            r"[A-Za-z_]\w*\s*\["
        ),
        re.compile(
            r"\bhk_generated_capability_inventory_get\s*\([^;{}]*\)\s*\{",
            re.DOTALL,
        ),
    )
    lines: list[int] = []
    for pattern in patterns:
        lines.extend(source.count("\n", 0, match.start()) + 1
                     for match in pattern.finditer(source))
    return sorted(set(lines))


def python_gated_provider_lines(source: str) -> list[int]:
    return [
        number for number, line in enumerate(source.splitlines(), 1)
        if re.search(r"\bHK_ENABLE_APP_[A-Z0-9_]+\b", line)
    ]


def phase2_source_failures() -> list[str]:
    policy = load_layer_policy()
    failures: list[str] = []
    graph: dict[str, list[str]] = {}
    files = repository_source_files(policy)
    for path in files:
        path_rel = repository_relative(path)
        layer = classify_repository_path(path_rel, policy)
        if layer is None:
            failures.append(f"{path_rel}: source has no explicit layer classification")
            continue
        source = path.read_text(encoding="utf-8")
        graph[path_rel] = []
        if layer in {"app", "adapter"}:
            for number in nonliteral_include_lines(source):
                failures.append(
                    f"{path_rel}:{number}: {layer} must use ordinary literal includes"
                )
            for number, token in sdk_token_lines(source):
                failures.append(
                    f"{path_rel}:{number}: K210 SDK token in {layer}: {token}"
                )
        for number, include in includes(path):
            target = resolve_repository_include(path, include)
            if target:
                graph[path_rel].append(target)
                if violation := layer_edge_violation(path_rel, target, policy):
                    failures.append(
                        f"{path_rel}:{number}: {violation}: {include}"
                    )
            elif violation := unresolved_hardware_violation(layer, include):
                failures.append(f"{path_rel}:{number}: {violation}: {include}")
    failures.extend(transitive_layer_failures(graph, policy))

    catalog = tomllib.loads(
        (ROOT / "platforms" / "k210" / "capabilities.toml").read_text(
            encoding="utf-8"
        )
    )
    for capability in catalog["capabilities"]:
        provider_path = ROOT / capability["provider_source"]
        source = provider_path.read_text(encoding="utf-8")
        for number in python_gated_provider_lines(source):
            failures.append(
                f"{capability['provider_source']}:{number}: hardware provider "
                "must not be gated by a feature-app macro"
            )
    for path in files:
        source = path.read_text(encoding="utf-8")
        for number in manual_provider_inventory_lines(source):
            failures.append(
                f"{repository_relative(path)}:{number}: provider inventory must "
                "be generated, not hand-declared"
            )
    for app in sorted((SRC / "apps").rglob("*")):
        if app.is_file() and app.suffix.casefold() in SOURCE_SUFFIXES:
            text = app.read_text(encoding="utf-8")
            for token in ("hk_sd.h", "frame_pool.h", "frame_pool_"):
                if token in text:
                    failures.append(
                        f"{relative(app)}: direct app dependency on {token} is forbidden"
                    )
    return sorted(set(failures))


def extract_repository_fragment(value: str) -> str | None:
    normalized = canonical_repository_path(value)
    folded = normalized.casefold()
    markers = (
        "firmware/include/", "firmware/src/", "firmware/assets/",
        "firmware/config/", "firmware/targets/", "platforms/", "boards/",
    )
    positions = [folded.find(marker) for marker in markers]
    positions = [position for position in positions if position >= 0]
    if not positions:
        return None
    fragment = normalized[min(positions):].rstrip(" \\:")
    return fragment


def dependency_repository_paths(source: str) -> list[str]:
    logical = source.replace("\\\r\n", " ").replace("\\\n", " ")
    result: list[str] = []
    for token in re.split(r"\s+", logical):
        if fragment := extract_repository_fragment(token):
            result.append(fragment)
    return list(dict.fromkeys(result))


def object_repository_source(path: Path) -> str | None:
    fragment = extract_repository_fragment(path.as_posix())
    if fragment and fragment.casefold().endswith(".obj"):
        fragment = fragment[:-4]
    elif fragment and fragment.casefold().endswith(".o"):
        fragment = fragment[:-2]
    return fragment


@dataclass(frozen=True)
class RepositoryObjectSymbols:
    object_path: str
    source: str
    layer: str
    defined: frozenset[str]
    undefined: frozenset[str]


def find_nm() -> str:
    for name in (
        "riscv64-unknown-elf-nm", "riscv64-unknown-elf-nm.exe", "nm", "nm.exe",
    ):
        if located := shutil.which(name):
            return located
    for name in ("riscv64-unknown-elf-nm.exe", "riscv64-unknown-elf-nm"):
        candidate = ROOT / "_deps" / "kendryte-toolchain" / "bin" / name
        if candidate.is_file():
            return str(candidate)
    raise RuntimeError("nm is required for architecture object validation")


def object_symbols(nm: str, path: Path, *, undefined: bool) -> set[str]:
    command = [
        nm, "-u" if undefined else "--defined-only", "--extern-only",
        str(path),
    ]
    result = subprocess.run(
        command, cwd=ROOT, text=True, capture_output=True, check=True,
    )
    symbols: set[str] = set()
    for line in result.stdout.splitlines():
        fields = line.split()
        if fields:
            symbols.add(fields[-1].lstrip("_"))
    return symbols


def forbidden_hardware_symbols(
    undefined: set[str], low_level_symbols: set[str],
) -> list[str]:
    return sorted(
        symbol for symbol in undefined
        if (symbol in low_level_symbols or
            symbol.startswith(FORBIDDEN_DIRECT_SYMBOL_PREFIXES))
    )


def repository_object_symbol_edge_failures(
    objects: list[RepositoryObjectSymbols],
    policy: dict[str, object] | None = None,
) -> list[str]:
    """Validate resolved object edges and unresolved hardware imports.

    Every externally defined repository symbol retains every defining object.
    Multiple definitions are therefore checked conservatively instead of being
    resolved by filesystem or linker traversal order.
    """

    policy = policy or load_layer_policy()
    ordered = sorted(
        objects, key=lambda item: (item.source, item.object_path, item.layer)
    )
    definitions: dict[str, list[RepositoryObjectSymbols]] = {}
    for item in ordered:
        for symbol in sorted(item.defined):
            definitions.setdefault(symbol, []).append(item)

    failures: list[str] = []
    for origin in ordered:
        for symbol in sorted(origin.undefined):
            targets = definitions.get(symbol, ())
            for target in targets:
                if target.object_path == origin.object_path:
                    continue
                violation = repository_layer_violation(
                    origin.layer, target.layer, policy
                )
                if violation:
                    failures.append(
                        f"{origin.source}: object symbol {symbol} resolves to "
                        f"forbidden {target.source}: {violation}"
                    )
            if targets or not forbidden_hardware_symbols({symbol}, set()):
                continue
            for target_layer in sorted(HARDWARE_LAYERS):
                violation = repository_layer_violation(
                    origin.layer, target_layer, policy
                )
                if violation:
                    failures.append(
                        f"{origin.source}: object has forbidden undefined "
                        f"hardware symbol {symbol}: {violation}"
                    )
                    break
    return sorted(set(failures))


def generated_dependency_failures(build_dir: Path) -> list[str]:
    policy = load_layer_policy()
    failures: list[str] = []
    for dependency in sorted(build_dir.rglob("*.obj.d")):
        paths = dependency_repository_paths(
            dependency.read_text(encoding="utf-8", errors="replace")
        )
        source = next(
            (item for item in paths
             if Path(item).suffix.casefold() in {".c", ".cc", ".cpp", ".cxx"}
             and classify_repository_path(item, policy) in {"app", "adapter"}),
            None,
        )
        if not source:
            continue
        for target in paths:
            if violation := layer_edge_violation(source, target, policy):
                failures.append(
                    f"{dependency.relative_to(build_dir).as_posix()}: generated "
                    f"dependency {source} -> {target}: {violation}"
                )
    return sorted(set(failures))


def object_undefined_symbol_failures(build_dir: Path) -> list[str]:
    policy = load_layer_policy()
    nm = find_nm()
    objects: list[RepositoryObjectSymbols] = []
    for path in sorted(build_dir.rglob("*.obj")):
        source = object_repository_source(path)
        if not source:
            continue
        layer = classify_repository_path(source, policy)
        if layer:
            objects.append(
                RepositoryObjectSymbols(
                    path.relative_to(build_dir).as_posix(), source, layer,
                    frozenset(object_symbols(nm, path, undefined=False)),
                    frozenset(object_symbols(nm, path, undefined=True)),
                )
            )
    return repository_object_symbol_edge_failures(objects, policy)


def provider_object_hashes(build_dir: Path) -> dict[str, str]:
    catalog = tomllib.loads(
        (ROOT / "platforms" / "k210" / "capabilities.toml").read_text(
            encoding="utf-8"
        )
    )
    objects = list(build_dir.rglob("*.obj"))
    result: dict[str, str] = {}
    for capability in catalog["capabilities"]:
        source = canonical_repository_path(capability["provider_source"])
        matches = [
            path for path in objects
            if canonical_repository_path(path).casefold().endswith(
                f"/{source}.obj".casefold()
            )
        ]
        if len(matches) != 1:
            raise RuntimeError(
                f"expected one built provider object for {source}, found {len(matches)}"
            )
        result[source] = hashlib.sha256(matches[0].read_bytes()).hexdigest()
    return result


def provider_hash_mismatches(
    disabled: dict[str, str], full: dict[str, str],
) -> list[str]:
    return [
        f"{source}: provider object differs between full and "
        "MicroPython-disabled profiles"
        for source in sorted(set(disabled) | set(full))
        if disabled.get(source) != full.get(source)
    ]


def verify_build_architecture(profile: str) -> list[str]:
    build_root = ROOT / "build" / "sen0305"
    build_dir = build_root / "sdk-full"
    if not build_dir.is_dir():
        raise RuntimeError(f"firmware build directory is missing: {build_dir}")
    failures = generated_dependency_failures(build_dir)
    failures.extend(object_undefined_symbol_failures(build_dir))
    hashes = provider_object_hashes(build_dir)
    snapshot = build_root / f"architecture-providers-{profile}.json"
    snapshot.write_text(
        json.dumps(hashes, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    if profile == "full":
        disabled = build_root / "architecture-providers-micropython-disabled.json"
        if not disabled.is_file():
            failures.append(
                "MicroPython-disabled provider hash snapshot is missing; verify "
                "that profile before full"
            )
        else:
            previous = json.loads(disabled.read_text(encoding="utf-8"))
            failures.extend(provider_hash_mismatches(previous, hashes))
    return sorted(set(failures))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--verify-build-profile",
        choices=("micropython-disabled", "full"),
        help=(
            "validate generated dependencies and object symbols, record provider "
            "hashes, and compare the full profile with the prior disabled build"
        ),
    )
    args = parser.parse_args(argv)
    failures = layout_failures()
    failures.extend(check_capabilities.validate())
    failures.extend(phase2_source_failures())
    for path in source_files():
        path_rel = relative(path)
        source_text = path.read_text(encoding="utf-8")
        if path_rel.startswith("apps/"):
            for number in nonliteral_include_lines(source_text):
                failures.append(
                    f"{path_rel}:{number}: apps must use literal include paths"
                )
        for number, include in includes(path):
            target = resolve_include(path, include)
            for violation in (
                layer_violation(path_rel, include, target),
                feature_include_violation(path_rel, target),
                settings_menu_violation(path_rel, target),
                ai_model_violation(path_rel, target),
            ):
                if violation:
                    failures.append(f"{path_rel}:{number}: {violation}: {include}")
        if not path_rel.startswith("apps/"):
            for number, line in enumerate(source_text.splitlines(), 1):
                if line.lstrip().startswith("#include"):
                    continue
                if PUBLIC_MUTABLE_EXTERN_RE.search(line):
                    failures.append(f"{path_rel}:{number}: public mutable extern variable")
        else:
            for number, token in sdk_token_lines(source_text):
                failures.append(
                    f"{path_rel}:{number}: K210 SDK token in app: {token}"
                )
    failures.extend(include_cycle_failures())
    if args.verify_build_profile:
        failures.extend(verify_build_architecture(args.verify_build_profile))
    if failures:
        print("[ARCH] boundary violations:")
        for failure in failures:
            print("  " + failure)
        return 1
    suffix = (
        f"; {args.verify_build_profile} generated/object evidence verified"
        if args.verify_build_profile else ""
    )
    print(
        f"[OK] architecture boundary guard v2 passed "
        f"({len(FEATURES)} declarative feature modules{suffix})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
