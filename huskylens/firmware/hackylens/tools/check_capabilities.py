#!/usr/bin/env python3
"""Validate Phase 2 capability composition inputs and generated invariants."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from board_contract import ContractError, load_board
import gen_capability_inventory as generator


INITIAL_IDS = {
    "hackylens.cap.time",
    "hackylens.cap.input",
    "hackylens.cap.display",
    "hackylens.cap.external-link",
    "hackylens.cap.lights",
}
EXPECTED_ABSENCE_CODES = {
    "resource-absent",
    "driver-unsupported",
    "route-unavailable",
    "provider-excluded",
    "version-incompatible",
    "feature-missing",
}


def validate(root: Path = ROOT) -> list[str]:
    failures: list[str] = []
    try:
        catalog_path = root / "platforms" / "k210" / "capabilities.toml"
        apps_path = root / "firmware" / "app_requirements.toml"
        consumers_path = root / "firmware" / "capability_consumers.toml"
        catalog = generator.load_catalog(catalog_path, root=root)
        apps = generator.load_app_requirements(apps_path)
        generator.load_consumer_requirements(consumers_path)
        if {item.id for item in catalog} != INITIAL_IDS:
            failures.append("platform capability catalog must contain exactly the five initial IDs")
        if generator.ABSENCE_CODES != EXPECTED_ABSENCE_CODES:
            failures.append("capability composer must expose exactly the six Phase 2.3 absence codes")
        composed_by_board: dict[str, generator.Composition] = {}
        for board_id in ("huskylens-sen0305", "sipeed-maix-cube"):
            board = load_board(board_id, root=root)
            first = generator.compose(
                board, set(apps), set(), set(), set(),
                app_requirements_path=apps_path,
                consumer_requirements_path=consumers_path,
                catalog_path=catalog_path,
                root=root,
            )
            second = generator.compose(
                board, set(apps), set(), set(), set(),
                app_requirements_path=apps_path,
                consumer_requirements_path=consumers_path,
                catalog_path=catalog_path,
                root=root,
            )
            composed_by_board[board_id] = first
            first_caps = generator.canonical_json_bytes(
                generator.capabilities_document(first)
            )
            second_caps = generator.canonical_json_bytes(
                generator.capabilities_document(second)
            )
            if first_caps != second_caps or generator.generated_c(first) != generator.generated_c(second):
                failures.append(f"{board_id}: capability generation is not deterministic")
            if any(item["code"] not in generator.ABSENCE_CODES for item in first.absences):
                failures.append(f"{board_id}: unknown absence reason")
            capability_doc = generator.capabilities_document(first)
            if board.support == "conformance" and capability_doc["runtime_supported"]:
                failures.append(f"{board_id}: conformance inventory claims runtime qualification")
        time = next(item for item in catalog if item.id == "hackylens.cap.time")
        if time.limits != (("max-sleep-us", 1, 300000000),):
            failures.append("time capability must publish the canonical finite sleep limit")
        input_capability = next(
            item for item in catalog if item.id == "hackylens.cap.input"
        )
        if input_capability.features != (
            "state", "events", "debounced-buttons"
        ) or input_capability.max_leases != 16:
            failures.append("input capability must publish the Phase 2.5 shared profile")
        display_capability = next(
            item for item in catalog if item.id == "hackylens.cap.display"
        )
        if display_capability.limits != (
            ("width", 1, 320), ("height", 2, 240)
        ):
            failures.append(
                "display capability must publish the required 320x240 limits"
            )
        if [item.id for item in composed_by_board["huskylens-sen0305"].capabilities] != [
            "hackylens.cap.time", "hackylens.cap.input",
            "hackylens.cap.display", "hackylens.cap.external-link",
            "hackylens.cap.lights"
        ]:
            failures.append(
                "SEN0305 inventory must contain exactly the five initial capabilities"
            )
        if [item.id for item in composed_by_board["sipeed-maix-cube"].capabilities] != [
            "hackylens.cap.time"
        ]:
            failures.append("Cube conformance inventory must keep Input absent")
        for app, requirements in generator.load_app_requirements(apps_path).items():
            if "display" in requirements.legacy:
                failures.append(f"{app}: private display requirement survived Phase 2.8")
            if not any(
                request.id == "hackylens.cap.display"
                for request in requirements.required
            ):
                failures.append(f"{app}: canonical required Display capability is missing")
            if "buttons" in requirements.legacy:
                failures.append(f"{app}: private buttons requirement survived Phase 2.5")
            if not any(
                request.id == "hackylens.cap.input"
                and request.features == ("state", "events", "debounced-buttons")
                for request in requirements.required
            ):
                failures.append(f"{app}: canonical required Input capability is missing")
        lights_apps = {
            "camera", "qr-camera", "face-detect", "apriltag",
            "object-detect", "settings", "sleep", "micropython",
        }
        for app, requirements in generator.load_app_requirements(apps_path).items():
            if "lights" in requirements.legacy:
                failures.append(f"{app}: private lights requirement survived Phase 2.6")
            if app in lights_apps and not any(
                request.id == "hackylens.cap.lights"
                for request in requirements.required
            ):
                failures.append(f"{app}: required Lights capability is missing")
    except (generator.CapabilityError, ContractError, OSError, ValueError) as exc:
        failures.append(str(exc))

    runtime = root / "firmware" / "src" / "runtime" / "capability_owner_runtime.c"
    if runtime.is_file():
        text = runtime.read_text(encoding="utf-8")
        if "hk_generated_capability_inventory_get" not in text:
            failures.append("production runtime does not use generated capability inventory")
        if "hk_capability_core_init(&s_capability_core, NULL, NULL, 0U)" in text:
            failures.append("production runtime still initializes a manual empty inventory")
    for base in (root / "firmware", root / "platforms"):
        for path in (
            item for item in base.rglob("*")
            if item.suffix.lower() in {".c", ".cc", ".cpp", ".cxx"}
        ):
            text = path.read_text(encoding="utf-8")
            if "void hk_generated_capability_inventory_get(" in text:
                failures.append(
                    f"{path.relative_to(root).as_posix()}: generated inventory symbol is manually defined"
                )
            if "capability_register(" in text or "capability_provider_register(" in text:
                failures.append(
                    f"{path.relative_to(root).as_posix()}: runtime capability registration is forbidden"
                )
    return failures


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args(argv)
    failures = validate()
    if failures:
        for failure in failures:
            print(f"[ERR] {failure}", file=sys.stderr)
        return 1
    print("[OK] capability composition and generated inventory guard passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
