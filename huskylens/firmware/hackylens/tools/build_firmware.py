#!/usr/bin/env python3
"""Build HackyLens firmware through Kendryte standalone SDK."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from board_contract import Board, ContractError, load_board
from check_board_ports import compile_conformance_board
from firmware_attestation import (
    FULL_APP_IDS as ATTESTED_FULL_APP_IDS,
    write as write_build_attestation,
)
from gen_board import generate as generate_board
from gen_flash_layout import load_validated, partition_by_name
import gen_capability_inventory as capability_inventory

WORKSPACE = ROOT.parent
LOCAL_DEPS = ROOT / "_deps"
LEGACY_DEPS = WORKSPACE / "hackylens-legacy" / "_deps"
K210_IMAGE_OVERHEAD = 37
MICROPYTHON_NATIVE_POLL_PATCH = (
    ROOT / "firmware" / "third_party" / "micropython" /
    "patches" / "0001-poll-native-iterators.patch"
)
# Commit timestamp of the pinned MicroPython v1.28.0 revision.  Its generator
# otherwise embeds the host calendar date and changes an identical firmware
# image when a qualification build crosses midnight.
MICROPYTHON_SOURCE_DATE_EPOCH = "1775481169"
SEMVER_RE = re.compile(
    r"[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?"
)

APP_MODULES = {
    "terminal": "HK_ENABLE_APP_TERMINAL",
    "camera": "HK_ENABLE_APP_CAMERA",
    "qr-camera": "HK_ENABLE_APP_QR_CAMERA",
    "face-detect": "HK_ENABLE_APP_FACE_DETECT",
    "apriltag": "HK_ENABLE_APP_APRILTAG",
    "object-detect": "HK_ENABLE_APP_OBJECT_DETECT",
    "files": "HK_ENABLE_APP_FILES",
    "buttons": "HK_ENABLE_APP_BUTTONS",
    "pong": "HK_ENABLE_APP_PONG",
    "settings": "HK_ENABLE_APP_SETTINGS",
    "sleep": "HK_ENABLE_APP_SLEEP",
    "micropython": "HK_ENABLE_APP_MICROPYTHON",
}

APP_SOURCE_DIRS = {
    "terminal": Path("firmware/src/apps/terminal"),
    "camera": Path("firmware/src/apps/camera"),
    "qr-camera": Path("firmware/src/apps/qr_camera"),
    "face-detect": Path("firmware/src/apps/face_detect"),
    "apriltag": Path("firmware/src/apps/apriltag"),
    "object-detect": Path("firmware/src/apps/object_detect"),
    "files": Path("firmware/src/apps/files"),
    "buttons": Path("firmware/src/apps/buttons"),
    "pong": Path("firmware/src/apps/pong"),
    "settings": Path("firmware/src/apps/settings"),
    "sleep": Path("firmware/src/apps/sleep"),
    "micropython": Path("firmware/src/apps/micropython"),
}
if set(APP_MODULES) != ATTESTED_FULL_APP_IDS:
    raise RuntimeError(
        "build app registry does not match firmware-attestation full composition"
    )

APP_REQUIREMENTS_PATH = ROOT / "firmware" / "app_requirements.toml"


def load_app_requirements(path: Path = APP_REQUIREMENTS_PATH) -> dict[str, set[str]]:
    """Compatibility view of schema-2 legacy private resource requirements."""

    requirements = capability_inventory.load_app_requirements(
        path, set(APP_MODULES)
    )
    return {app: set(value.legacy) for app, value in requirements.items()}


def compose_capabilities(
    board: Board,
    disabled_apps: set[str],
    required_apps: set[str],
    disabled_capabilities: set[str],
    *,
    allow_required_consumer_exclusion: bool = False,
) -> capability_inventory.Composition:
    try:
        return capability_inventory.compose(
            board,
            set(APP_MODULES),
            disabled_apps,
            required_apps,
            disabled_capabilities,
            allow_required_consumer_exclusion=allow_required_consumer_exclusion,
        )
    except capability_inventory.CapabilityError as exc:
        raise RuntimeError(str(exc)) from exc


def compose_apps(board: Board, disabled_apps: set[str],
                 required_apps: set[str]) -> tuple[set[str], list[dict[str, object]]]:
    """Compatibility tuple for callers that only need app exclusions."""

    composition = compose_capabilities(
        board, disabled_apps, required_apps, set()
    )
    return set(composition.disabled_apps), list(composition.exclusions)


CAMERA_FEATURE_SOURCE_MODULES = {
    Path("firmware/src/controllers/camera_runtime_controller.c"),
    Path("firmware/src/controllers/camera_runtime_controller.h"),
    Path("firmware/src/controllers/debug_camera_controller.c"),
    Path("firmware/src/controllers/debug_camera_controller.h"),
    Path("firmware/src/drivers/camera_stream.c"),
    Path("firmware/src/drivers/camera_stream.h"),
    Path("firmware/src/drivers/ov2640_sensor.c"),
    Path("firmware/src/drivers/ov2640_sensor.h"),
    Path("platforms/k210/hal/hal_dvp.c"),
    Path("platforms/k210/hal/hal_dvp.h"),
}
for camera_path in (ROOT / "firmware" / "src" / "services").glob("camera_*"):
    CAMERA_FEATURE_SOURCE_MODULES.add(camera_path.relative_to(ROOT))
for camera_path in (ROOT / "firmware" / "src" / "services" / "internal").glob("camera_*"):
    CAMERA_FEATURE_SOURCE_MODULES.add(camera_path.relative_to(ROOT))
for camera_path in (ROOT / "firmware" / "src" / "ui").glob("camera_*"):
    CAMERA_FEATURE_SOURCE_MODULES.add(camera_path.relative_to(ROOT))

CAMERA_AI_INPUT_SOURCE_MODULES = {
    Path("firmware/src/services/camera_ai_input.c"),
    Path("firmware/src/services/camera_ai_input.h"),
}

CORE1_EXECUTOR_SOURCE_MODULES = {
    Path("firmware/src/services/core1_executor.c"),
    Path("firmware/src/services/core1_executor.h"),
}

MICROPYTHON_FEATURE_SOURCE_MODULES = {
    Path("firmware/src/internal/boot_internal.c"),
    Path("firmware/src/internal/boot_internal.h"),
    Path("platforms/k210/hal/hal_watchdog.c"),
    Path("platforms/k210/hal/hal_watchdog.h"),
    Path("firmware/src/services/hmpy_codec.c"),
    Path("firmware/src/services/hmpy_codec.h"),
    Path("firmware/src/services/hmpy_session.c"),
    Path("firmware/src/services/hmpy_session.h"),
    Path("firmware/src/services/micropython_program.c"),
    Path("firmware/src/services/micropython_program.h"),
    Path("firmware/src/adapters/micropython/micropython_capability_bridge.c"),
    Path("firmware/src/adapters/micropython/micropython_capability_bridge.h"),
    Path("firmware/src/services/micropython_port.c"),
    Path("firmware/src/services/micropython_runtime.c"),
    Path("firmware/src/services/micropython_runtime.h"),
    Path("firmware/src/storage/userfs.c"),
    Path("firmware/src/storage/userfs.h"),
}

TARGETS = {
    "full": {
        "project": "hackylens_full",
        "output": "hackylens-full.bin",
        "build_dir": "sdk-full",
        "target_source": ROOT / "firmware" / "targets" / "full.c",
    },
}


def dep_roots() -> list[Path]:
    return [LOCAL_DEPS, LEGACY_DEPS]


def find_sdk() -> Path | None:
    candidates: list[Path] = []
    if os.environ.get("KENDRYTE_SDK_DIR"):
        candidates.append(Path(os.environ["KENDRYTE_SDK_DIR"]))
    for root in dep_roots():
        candidates.append(root / "kendryte-standalone-sdk")
    for path in candidates:
        if (path / "CMakeLists.txt").is_file() and (path / "lib").is_dir():
            return path.resolve()
    return None


def find_toolchain_bin() -> Path | None:
    candidates: list[Path] = []
    if os.environ.get("KENDRYTE_TOOLCHAIN_BIN"):
        candidates.append(Path(os.environ["KENDRYTE_TOOLCHAIN_BIN"]))
    for name in ("riscv64-unknown-elf-gcc.exe", "riscv64-unknown-elf-gcc"):
        exe = shutil.which(name)
        if exe:
            candidates.append(Path(exe).parent)
    for root in dep_roots():
        if root.is_dir():
            for name in ("riscv64-unknown-elf-gcc.exe", "riscv64-unknown-elf-gcc"):
                candidates.extend(path.parent for path in root.rglob(name) if path.is_file())
    for path in candidates:
        for exe_name in ("riscv64-unknown-elf-gcc.exe", "riscv64-unknown-elf-gcc"):
            if (path / exe_name).is_file():
                return path.resolve()
    return None


def find_micropython() -> Path | None:
    candidates: list[Path] = []
    if os.environ.get("HACKYLENS_MICROPYTHON_DIR"):
        candidates.append(Path(os.environ["HACKYLENS_MICROPYTHON_DIR"]))
    for root in dep_roots():
        candidates.append(root / "micropython")
    for path in candidates:
        if (path / "ports" / "embed" / "embed.mk").is_file():
            return path.resolve()
    return None


def find_littlefs() -> Path | None:
    candidates: list[Path] = []
    if os.environ.get("HACKYLENS_LITTLEFS_DIR"):
        candidates.append(Path(os.environ["HACKYLENS_LITTLEFS_DIR"]))
    for root in dep_roots():
        candidates.append(root / "littlefs")
    for path in candidates:
        if all((path / name).is_file() for name in
               ("lfs.c", "lfs.h", "lfs_util.c", "lfs_util.h")):
            return path.resolve()
    return None


def run(cmd: list[str], cwd: Path | None = None) -> None:
    print("+ " + " ".join(str(part) for part in cmd))
    subprocess.run(cmd, cwd=cwd, check=True)


def read_firmware_version(path: Path | None = None) -> str:
    version_path = path if path is not None else ROOT / "VERSION"
    version = version_path.read_text(encoding="utf-8").strip()
    if not SEMVER_RE.fullmatch(version):
        raise RuntimeError(f"invalid semantic version in {version_path}: {version!r}")
    return version


def cmake_path(path: Path) -> str:
    return str(path).replace("\\", "/")


def find_git_bash() -> tuple[Path, Path] | None:
    git = shutil.which("git")
    if not git:
        return None
    git_path = Path(git).resolve()
    for root in (git_path.parent.parent, git_path.parent):
        bash = root / "bin" / "bash.exe"
        usr_bin = root / "usr" / "bin"
        if bash.is_file() and usr_bin.is_dir():
            return bash, usr_bin
    return None


def apply_micropython_native_poll_patch(package: Path) -> None:
    git = shutil.which("git")
    if not git:
        raise RuntimeError("git is required to apply the pinned MicroPython port patch")
    if not MICROPYTHON_NATIVE_POLL_PATCH.is_file():
        raise RuntimeError(
            f"MicroPython native-poll patch not found: {MICROPYTHON_NATIVE_POLL_PATCH}"
        )
    runtime_source = package / "py" / "runtime.c"
    if not runtime_source.is_file():
        raise RuntimeError(f"MicroPython runtime source not found: {runtime_source}")

    # `git apply --directory` resolves against the enclosing repository's root,
    # and also refuses paths outside the invocation prefix. Anchoring the value
    # to the repository root keeps both rules satisfied whether this tree is
    # the repository itself or a subdirectory of a larger one. Getting this
    # wrong is silent: git reports "Skipped patch" and still exits 0.
    anchor = ROOT
    try:
        toplevel = subprocess.run(
            [git, "-C", str(ROOT), "rev-parse", "--show-toplevel"],
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        toplevel = ""
    if toplevel:
        anchor = Path(toplevel)

    try:
        package_relative = package.resolve().relative_to(anchor.resolve())
    except ValueError:
        apply_cwd = package
        directory_arg: list[str] = []
    else:
        apply_cwd = ROOT
        directory_arg = [f"--directory={cmake_path(package_relative)}"]
    base_cmd = [git, "apply", *directory_arg]
    run([*base_cmd, "--check", str(MICROPYTHON_NATIVE_POLL_PATCH)], cwd=apply_cwd)
    run([*base_cmd, str(MICROPYTHON_NATIVE_POLL_PATCH)], cwd=apply_cwd)
    patched = runtime_source.read_text(encoding="utf-8")
    if patched.count("MICROPY_PORT_ITERNEXT_HOOK") != 2:
        raise RuntimeError("MicroPython native iterator poll patch is incomplete")


def micropython_build_environment() -> dict[str, str]:
    env = os.environ.copy()
    # Exact-image builds must not inherit a caller-selected wall-clock value.
    env["SOURCE_DATE_EPOCH"] = MICROPYTHON_SOURCE_DATE_EPOCH
    return env


def generate_micropython_embed(micropython: Path) -> Path:
    config_dir = ROOT / "firmware" / "third_party" / "micropython"
    makefile = config_dir / "micropython_embed.mk"
    work = ROOT / "build" / "micropython-embed-work"
    package = ROOT / "build" / "micropython_embed"
    make = shutil.which("mingw32-make") if os.name == "nt" else shutil.which("make")
    if not make:
        raise RuntimeError("GNU make is required to generate the MicroPython embed package")

    env = micropython_build_environment()
    makefile_arg = cmake_path(Path(os.path.relpath(makefile, config_dir)))
    micropython_arg = cmake_path(Path(os.path.relpath(micropython, config_dir)))
    work_arg = cmake_path(Path(os.path.relpath(work, config_dir)))
    package_arg = cmake_path(Path(os.path.relpath(package, config_dir)))
    cmd = [
        make,
        "-f", makefile_arg,
        f"MICROPYTHON_TOP={micropython_arg}",
        f"BUILD={work_arg}",
        f"PACKAGE_DIR={package_arg}",
        f"PYTHON={cmake_path(Path(sys.executable))}",
    ]
    if os.name == "nt":
        git_bash = find_git_bash()
        if not git_bash:
            raise RuntimeError("Git for Windows bash/coreutils are required to generate MicroPython")
        bash, usr_bin = git_bash
        env["PATH"] = str(usr_bin) + os.pathsep + env.get("PATH", "")
        cmd.append(f"SHELL={cmake_path(bash)}")

    print(f"[GENERATE] MicroPython embed from {micropython}")
    print("+ " + " ".join(str(part) for part in cmd))
    subprocess.run(cmd, cwd=config_dir, env=env, check=True)
    required = (
        package / "genhdr" / "qstrdefs.generated.h",
        package / "genhdr" / "moduledefs.h",
        package / "port" / "micropython_embed.h",
    )
    if any(not path.is_file() for path in required):
        raise RuntimeError(f"MicroPython embed generator produced an incomplete package: {package}")
    apply_micropython_native_poll_patch(package)
    return package


def write_project_cmake(stage: Path, package: Path | None, board: Board) -> None:
    lines = [
        "# Generated by tools/build_firmware.py",
        "# The legacy SDK recursively promotes every header directory to -I.",
        "# A public capability/time.h must not shadow the C library <time.h>.",
        "set(_HACKYLENS_CAPABILITY_LEAF \"${CMAKE_CURRENT_LIST_DIR}/firmware/include/hackylens/capability\")",
        "function(_hackylens_prune_capability_leaf directory)",
        "    get_property(_targets DIRECTORY \"${directory}\" PROPERTY BUILDSYSTEM_TARGETS)",
        "    foreach(_target IN LISTS _targets)",
        "        get_target_property(_includes ${_target} INCLUDE_DIRECTORIES)",
        "        if(_includes)",
        "            list(REMOVE_ITEM _includes \"${_HACKYLENS_CAPABILITY_LEAF}\")",
        "            set_property(TARGET ${_target} PROPERTY INCLUDE_DIRECTORIES \"${_includes}\")",
        "        endif()",
        "    endforeach()",
        "    get_property(_subdirectories DIRECTORY \"${directory}\" PROPERTY SUBDIRECTORIES)",
        "    foreach(_subdirectory IN LISTS _subdirectories)",
        "        _hackylens_prune_capability_leaf(\"${_subdirectory}\")",
        "    endforeach()",
        "endfunction()",
        "_hackylens_prune_capability_leaf(\"${SDK_ROOT}\")",
        "target_include_directories(${PROJECT_NAME} PRIVATE",
        f'    "{cmake_path(stage)}"',
        f'    "{cmake_path(stage / "firmware" / "include")}"',
        f'    "{cmake_path(stage / "platforms" / "k210" / "hal")}"',
        ")",
    ]
    if package is not None:
        sources = [
            path for path in sorted(package.rglob("*.c"))
            if path.relative_to(package).as_posix() != "port/mphalport.c"
        ]
        if not sources:
            raise RuntimeError(f"MicroPython embed package contains no C sources: {package}")
        lines.append("set(HACKYLENS_MICROPYTHON_SOURCES")
        lines.extend(f'    "{cmake_path(path)}"' for path in sources)
        lines.extend([
            ")",
            "target_sources(${PROJECT_NAME} PRIVATE ${HACKYLENS_MICROPYTHON_SOURCES})",
            "target_include_directories(${PROJECT_NAME} PRIVATE",
            f'    "{cmake_path(ROOT / "firmware" / "third_party" / "micropython")}"',
            f'    "{cmake_path(package)}"',
            ")",
            "target_compile_definitions(${PROJECT_NAME} PRIVATE",
            "    LFS_NO_MALLOC",
            "    LFS_NAME_MAX=63",
            ")",
        ])
    lines.append("")
    (stage / "project.cmake").write_text("\n".join(lines), encoding="utf-8")


def write_reproducible_path_map(path: Path, sdk: Path) -> None:
    """Map host-specific source roots before any SDK target is created."""
    # GCC resolves overlapping prefix maps by the last matching option.  The
    # SDK normally lives below ROOT locally but may be supplied from an
    # external cache in CI, so emit the broad workspace mapping first and the
    # SDK-specific mapping last.  Both layouts then produce /hackylens/sdk.
    roots = [
        (ROOT.resolve(), "/hackylens/workspace"),
        (sdk.resolve(), "/hackylens/sdk"),
    ]
    seen: set[str] = set()
    flags: list[str] = []
    for source, destination in roots:
        for encoded in (cmake_path(source), str(source)):
            if encoded in seen:
                continue
            seen.add(encoded)
            flags.append(f"-ffile-prefix-map={encoded}={destination}")
            # GCC applies -ffile-prefix-map to C/C++ paths. GAS also records
            # its own working/source paths for preprocessed .S inputs, so map
            # those explicitly to keep the complete ELF path-independent.
            flags.append(f"-Wa,--debug-prefix-map,{encoded}={destination}")
    lines = [
        "# Generated by tools/build_firmware.py",
        "# CMAKE_PROJECT_INCLUDE runs at the end of project(), before SDK targets.",
        "add_compile_options(",
        *(f'    "{flag.replace(chr(92), chr(92) * 2)}"' for flag in flags),
        ")",
        "",
    ]
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def reject_embedded_host_paths(artifact: Path, roots: list[Path]) -> None:
    """Reject a release input that still embeds a local source/build prefix."""
    content = artifact.read_bytes().lower()
    leaked: list[str] = []
    for root in roots:
        resolved = root.resolve()
        candidates = {
            str(resolved),
            cmake_path(resolved),
            str(resolved).replace("\\", "/"),
            str(resolved).replace("/", "\\"),
        }
        if any(candidate.encode("utf-8").lower() in content for candidate in candidates):
            leaked.append(str(resolved))
    if leaked:
        raise RuntimeError(
            f"build artifact embeds host path(s): {', '.join(sorted(set(leaked)))}"
        )


def copy_tree_files(src: Path, dst: Path) -> None:
    if not src.is_dir():
        return
    for path in src.rglob("*"):
        if path.is_file():
            rel = path.relative_to(src)
            out = dst / rel
            out.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, out)


def stage_firmware_sources(stage: Path, disabled_apps: set[str]) -> None:
    disabled_dirs = {APP_SOURCE_DIRS[app] for app in disabled_apps}
    camera_feature_enabled = ("camera" not in disabled_apps or
                              "qr-camera" not in disabled_apps or
                              "face-detect" not in disabled_apps or
                              "apriltag" not in disabled_apps or
                              "object-detect" not in disabled_apps)
    camera_ai_input_enabled = ("face-detect" not in disabled_apps or
                               "object-detect" not in disabled_apps)
    core1_executor_enabled = ("apriltag" not in disabled_apps or
                              "micropython" not in disabled_apps)
    micropython_feature_enabled = "micropython" not in disabled_apps

    for path in (ROOT / "firmware" / "src").rglob("*"):
        if not path.is_file():
            continue
        rel = path.relative_to(ROOT)
        if rel.suffix == ".inc":
            continue
        if any(rel.is_relative_to(disabled_dir) for disabled_dir in disabled_dirs):
            print(f"[SKIP] disabled app source {rel}")
            continue
        if not camera_ai_input_enabled and rel in CAMERA_AI_INPUT_SOURCE_MODULES:
            print(f"[SKIP] unused camera AI input source {rel}")
            continue
        if not core1_executor_enabled and rel in CORE1_EXECUTOR_SOURCE_MODULES:
            print(f"[SKIP] unused core-1 executor source {rel}")
            continue
        if (not micropython_feature_enabled and
                rel in MICROPYTHON_FEATURE_SOURCE_MODULES):
            print(f"[SKIP] unused MicroPython source {rel}")
            continue
        if not camera_feature_enabled and rel in CAMERA_FEATURE_SOURCE_MODULES:
            print(f"[SKIP] unused camera source {rel}")
            continue
        out = stage / rel
        out.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, out)


def stage_platform_sources(
    stage: Path,
    disabled_apps: set[str],
    capability_composition: capability_inventory.Composition,
) -> None:
    camera_feature_enabled = ("camera" not in disabled_apps or
                              "qr-camera" not in disabled_apps or
                              "face-detect" not in disabled_apps or
                              "apriltag" not in disabled_apps or
                              "object-detect" not in disabled_apps)
    micropython_feature_enabled = "micropython" not in disabled_apps
    provider_sources = {
        Path(item.provider_source) for item in capability_inventory.load_catalog()
    }
    selected_provider_sources = {
        Path(item.provider_source) for item in capability_composition.capabilities
    }
    platform = ROOT / "platforms" / "k210"
    for path in platform.rglob("*"):
        if not path.is_file() or path.name in {"devices.toml", "capabilities.toml"}:
            continue
        rel = path.relative_to(ROOT)
        if rel in provider_sources and rel not in selected_provider_sources:
            print(f"[SKIP] excluded capability provider source {rel}")
            continue
        if not camera_feature_enabled and rel in CAMERA_FEATURE_SOURCE_MODULES:
            print(f"[SKIP] unused camera platform source {rel}")
            continue
        if not micropython_feature_enabled and rel in MICROPYTHON_FEATURE_SOURCE_MODULES:
            print(f"[SKIP] unused MicroPython platform source {rel}")
            continue
        out = stage / rel
        out.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, out)


def stage_board_port(stage: Path, board: Board) -> None:
    destination = stage / "boards" / board.id
    destination.mkdir(parents=True, exist_ok=True)
    shutil.copy2(board.directory / "board.c", destination / "board.c")
    for name in ("pins.h", "defaults.h", "inventory.h", "flash_layout.h"):
        source = board.generated_dir / name
        shutil.copy2(source, stage / name)
    shutil.copy2(
        ROOT / "firmware" / "src" / "internal" / "hk_board_port.h",
        stage / "hk_board_port.h",
    )


def write_config(stage: Path, disabled_apps: set[str],
                 wdt_fault_injection: bool = False,
                 disable_dristy: bool = False) -> None:
    version = read_firmware_version()
    if wdt_fault_injection:
        version += "-wdtfi"
    lines = [
        "#ifndef HK_CONFIG_H",
        "#define HK_CONFIG_H",
        f'#define HACKYLENS_VERSION "{version}"',
        f"#define HK_ENABLE_DRISTY {0 if disable_dristy else 1}",
        f'#define DRISTY_VERSION "1.0.0"',
    ]
    for app, flag in APP_MODULES.items():
        lines.append(f"#define {flag} {0 if app in disabled_apps else 1}")
    lines.extend([
        f"#define HK_MICROPYTHON_WDT_FAULT_INJECTION {1 if wdt_fault_injection else 0}",
        "#define HK_ENABLE_CAMERA_FEATURE (HK_ENABLE_APP_CAMERA || HK_ENABLE_APP_QR_CAMERA || HK_ENABLE_APP_FACE_DETECT || HK_ENABLE_APP_APRILTAG || HK_ENABLE_APP_OBJECT_DETECT)",
        "#define HK_ENABLE_QR_FEATURE HK_ENABLE_APP_QR_CAMERA",
        '#include "hk_config_default.h"',
        "#endif",
        "",
    ])
    (stage / "hk_config.h").write_text("\n".join(lines), encoding="utf-8")


def stage_target(sdk: Path, target_name: str, board: Board,
                  disabled_apps: set[str],
                  micropython_package: Path | None = None,
                  littlefs: Path | None = None,
                  wdt_fault_injection: bool = False,
                  disable_dristy: bool = False,
                  capability_composition: capability_inventory.Composition | None = None) -> Path:
    target = TARGETS[target_name]
    stage = sdk / "src" / str(target["project"])
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True, exist_ok=True)

    if capability_composition is None:
        capability_composition = compose_capabilities(
            board, disabled_apps, set(), set()
        )

    shutil.copy2(Path(target["target_source"]), stage / "main.c")
    stage_firmware_sources(stage, disabled_apps)
    copy_tree_files(ROOT / "firmware" / "include", stage / "firmware" / "include")
    stage_platform_sources(stage, disabled_apps, capability_composition)
    stage_board_port(stage, board)

    for header in (ROOT / "firmware" / "assets").glob("*.h"):
        shutil.copy2(header, stage / header.name)
    shutil.copy2(ROOT / "firmware" / "config" / "hk_config_default.h", stage / "hk_config_default.h")

    if "qr-camera" not in disabled_apps:
        quirc = ROOT / "firmware" / "third_party" / "quirc"
        for source in quirc.glob("*.c"):
            shutil.copy2(source, stage / source.name)
        for header in quirc.glob("*.h"):
            shutil.copy2(header, stage / header.name)

    if "apriltag" not in disabled_apps:
        copy_tree_files(ROOT / "firmware" / "third_party" / "apriltag", stage)

    if littlefs is not None:
        for name in ("lfs.c", "lfs.h", "lfs_util.c", "lfs_util.h"):
            shutil.copy2(littlefs / name, stage / name)

    capability_inventory.write_generated_c(capability_composition, stage)
    write_config(stage, disabled_apps, wdt_fault_injection, disable_dristy)
    write_project_cmake(stage, micropython_package, board)
    return stage


def build_target(name: str, board: Board, sdk: Path, toolchain_bin: Path,
                 disabled_apps: set[str], exclusions: list[dict[str, object]],
                 capability_composition: capability_inventory.Composition,
                 wdt_fault_injection: bool = False,
                 disable_dristy: bool = False) -> Path:
    target = TARGETS[name]
    project = str(target["project"])
    output_name = Path(str(target["output"]))
    if wdt_fault_injection:
        output_name = output_name.with_name(
            f"{output_name.stem}-wdtfi{output_name.suffix}"
        )
        print("[DANGER] building non-release WDT fault-injection firmware")
    out_image = ROOT / "build" / board.id / output_name
    micropython_package = None
    littlefs = None
    if "micropython" not in disabled_apps:
        micropython = find_micropython()
        if not micropython:
            raise RuntimeError(
                "pinned MicroPython checkout not found; run python tools\\bootstrap_deps.py"
            )
        micropython_package = generate_micropython_embed(micropython)
        littlefs = find_littlefs()
        if not littlefs:
            raise RuntimeError(
                "pinned littlefs checkout not found; run python tools\\bootstrap_deps.py"
            )
    stage = stage_target(
        sdk, name, board, disabled_apps, micropython_package, littlefs,
        wdt_fault_injection, disable_dristy, capability_composition,
    )
    print(f"[STAGE] {stage}")

    build_dir = ROOT / "build" / board.id / str(target["build_dir"])
    if build_dir.exists():
        shutil.rmtree(build_dir)
    build_dir.mkdir(parents=True, exist_ok=True)
    path_map = build_dir / "hackylens-path-map.cmake"
    write_reproducible_path_map(path_map, sdk)

    generator = "MinGW Makefiles" if os.name == "nt" else "Ninja"
    cmake = shutil.which("cmake")
    if not cmake:
        raise RuntimeError("cmake not found")

    run([
        cmake,
        "-S",
        cmake_path(sdk),
        "-B",
        cmake_path(build_dir),
        "-G",
        generator,
        f"-DPROJ={project}",
        f"-DTOOLCHAIN={cmake_path(toolchain_bin)}",
        "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
        f"-DCMAKE_PROJECT_INCLUDE={cmake_path(path_map)}",
    ])
    run([cmake, "--build", cmake_path(build_dir), "--parallel"])

    built = build_dir / f"{project}.bin"
    if not built.is_file() or built.stat().st_size == 0:
        raise RuntimeError(f"build did not produce a non-empty image: {built}")
    built_elf = build_dir / project
    if not built_elf.is_file() or built_elf.stat().st_size == 0:
        raise RuntimeError(f"build did not produce a non-empty ELF: {built_elf}")
    reject_embedded_host_paths(built, [ROOT, sdk])
    reject_embedded_host_paths(built_elf, [ROOT, sdk])
    flash, partitions = load_validated(board.flash_layout_path)
    firmware_partition = partition_by_name(partitions, "firmware")
    flash_limit = firmware_partition["offset"] + firmware_partition["size"]
    flashed_size = ((built.stat().st_size + K210_IMAGE_OVERHEAD + flash["erase_size"] - 1)
                    // flash["erase_size"] * flash["erase_size"])
    if firmware_partition["offset"] + flashed_size > flash_limit:
        raise RuntimeError(
            f"firmware image exceeds canonical firmware partition "
            f"0x{flash_limit:06X}: "
            f"raw={built.stat().st_size} flashed={flashed_size} bytes"
        )

    out_image.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(built, out_image)
    attestation = out_image.with_suffix(".attestation.json")
    capabilities_path = ROOT / "build" / board.id / "capabilities.json"
    capabilities_sha256 = hashlib.sha256(capabilities_path.read_bytes()).hexdigest()
    write_build_attestation(
        attestation,
        out_image,
        board,
        target=name,
        disabled_apps=disabled_apps,
        exclusions=exclusions,
        disabled_capabilities=set(capability_composition.disabled_capabilities),
        capabilities_sha256=capabilities_sha256,
        wdt_fault_injection=wdt_fault_injection,
    )
    if wdt_fault_injection:
        print("[DANGER] isolated test image kept under build/; dist/ was not touched")
    elif disabled_apps or capability_composition.disabled_capabilities:
        print(
            "[BUILD] feature-modified image kept under build/; qualified dist/ "
            "artifacts were not replaced"
        )
    else:
        run([sys.executable, str(ROOT / "tools" / "make_image.py"),
             str(out_image), "--board", board.id,
             "--attestation", str(attestation),
             "--out-dir", str(ROOT / "dist")])
    print(f"[OK] {out_image} ({out_image.stat().st_size} bytes)")
    return out_image


def conformance_check(board: Board) -> None:
    if board.support != "conformance":
        raise RuntimeError("conformance target requires support=conformance")
    failures = generate_board(board, check=True)
    if failures:
        raise RuntimeError("; ".join(failures))
    compile_conformance_board(board)
    print(f"[OK] {board.id}: conformance descriptor/BSP compile-check passed")


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build HackyLens full firmware")
    parser.add_argument("target", choices=sorted(set(TARGETS) | {"conformance"}))
    parser.add_argument("--board", required=True, help="Canonical board.toml ID")
    parser.add_argument(
        "--require-app",
        action="append",
        default=[],
        choices=sorted(APP_MODULES),
        help="Fail instead of board-excluding this app. Can be repeated.",
    )
    parser.add_argument(
        "--disable-app",
        action="append",
        default=[],
        choices=sorted(APP_MODULES),
        help="Disable an app in the menu registry. Can be repeated.",
    )
    parser.add_argument(
        "--disable-capability",
        action="append",
        default=[],
        choices=sorted(item.id for item in capability_inventory.load_catalog()),
        help=(
            "Diagnostic-only capability exclusion. Can be repeated and always "
            "makes a runtime build non-release-qualified."
        ),
    )
    parser.add_argument(
        "--disable-dristy",
        action="store_true",
        help="Omit Dristy runtime from firmware (HackyLens-only build)",
    )
    parser.add_argument(
        "--wdt-fault-injection",
        action="store_true",
        help=(
            "DANGEROUS TEST BUILD: wedge core 1 on MicroPython STOP/deadline "
            "to exercise the WDT1 recovery path"
        ),
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        board = load_board(args.board)
    except ContractError as exc:
        print(f"[ERR] {exc}", file=sys.stderr)
        return 2
    failures = generate_board(board, check=True)
    if failures:
        for failure in failures:
            print(f"[ERR] {failure}", file=sys.stderr)
        return 2
    composition = compose_capabilities(
        board,
        set(args.disable_app),
        set(args.require_app),
        set(args.disable_capability),
        allow_required_consumer_exclusion=bool(args.disable_capability),
    )
    capability_inventory.write_artifacts(
        composition, ROOT / "build" / board.id
    )
    for exclusion in composition.exclusions:
        print(json.dumps({"event": "app-excluded", **exclusion}, sort_keys=True))
    for exclusion in composition.required_consumer_exclusions:
        print(json.dumps(
            {"event": "required-consumer-excluded", **exclusion}, sort_keys=True
        ))
    for fallback in composition.optional_fallbacks:
        print(json.dumps({"event": "optional-fallback", **fallback}, sort_keys=True))
    if args.target == "conformance":
        conformance_check(board)
        return 0
    if board.support != "runtime":
        print(
            f"[ERR] board {board.id!r} is conformance-only; full firmware is unavailable",
            file=sys.stderr,
        )
        return 2
    sdk = find_sdk()
    if not sdk:
        print("[ERR] Kendryte SDK not found. Run: python hackylens\\tools\\bootstrap_deps.py", file=sys.stderr)
        return 1
    toolchain = find_toolchain_bin()
    if not toolchain:
        print("[ERR] Kendryte toolchain not found. Run python tools\\bootstrap_deps.py and . .\\env.ps1", file=sys.stderr)
        return 1

    disabled_apps = set(composition.disabled_apps)
    exclusions = list(composition.exclusions)
    if args.wdt_fault_injection and "micropython" in disabled_apps:
        raise RuntimeError(
            "--wdt-fault-injection requires the micropython app"
        )
    build_target(
        args.target, board, sdk, toolchain, disabled_apps, exclusions,
        composition, args.wdt_fault_injection, args.disable_dristy,
    )
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        raise SystemExit(130)
    except subprocess.CalledProcessError as exc:
        print(f"[ERR] command failed with exit code {exc.returncode}", file=sys.stderr)
        raise SystemExit(exc.returncode)
    except Exception as exc:
        print(f"[ERR] {exc}", file=sys.stderr)
        raise SystemExit(1)
