#!/usr/bin/env python3
"""Generate immutable Capability API inventory and private owner grants."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import sys
try:
    import tomllib
except ModuleNotFoundError:  # Python 3.10 and older
    import tomli as tomllib
from typing import Any, Mapping


ROOT = Path(__file__).resolve().parents[1]
if str(Path(__file__).resolve().parent) not in sys.path:
    sys.path.insert(0, str(Path(__file__).resolve().parent))

from board_contract import Board, ContractError, load_board


CATALOG_PATH = ROOT / "platforms" / "k210" / "capabilities.toml"
APP_REQUIREMENTS_PATH = ROOT / "firmware" / "app_requirements.toml"
CONSUMER_REQUIREMENTS_PATH = ROOT / "firmware" / "capability_consumers.toml"
CAPABILITY_ID_RE = re.compile(r"^dristy\.cap\.[a-z][a-z0-9]*(?:-[a-z0-9]+)*$")
IDENTIFIER_RE = re.compile(r"^[a-z][a-z0-9]*(?:-[a-z0-9]+)*$")
C_SYMBOL_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
SOURCE_RE = re.compile(r"^(?:[a-zA-Z0-9_.-]+/)*[a-zA-Z0-9_.-]+\.c$")
ABSENCE_CODES = frozenset({
    "resource-absent",
    "driver-unsupported",
    "route-unavailable",
    "provider-excluded",
    "version-incompatible",
    "feature-missing",
})
REQUEST_FIELDS = {
    "id", "instance", "minimum", "maximum_exclusive", "features",
}
OPTIONAL_REQUEST_FIELDS = REQUEST_FIELDS | {"fallback"}


class CapabilityError(ValueError):
    """Capability composition input or generation is invalid."""


@dataclass(frozen=True, order=True)
class Version:
    major: int
    minor: int
    patch: int

    def c_fields(self) -> str:
        return f"{{{self.major}U, {self.minor}U, {self.patch}U, 0U}}"

    def __str__(self) -> str:
        return f"{self.major}.{self.minor}.{self.patch}"


@dataclass(frozen=True)
class CapabilityRequest:
    id: str
    instance: int
    minimum: Version
    maximum_exclusive: Version
    features: tuple[str, ...]
    fallback: str | None = None


@dataclass(frozen=True)
class Requirements:
    legacy: tuple[str, ...]
    required: tuple[CapabilityRequest, ...]
    optional: tuple[CapabilityRequest, ...]
    enabled_by_app: str | None = None


@dataclass(frozen=True)
class Capability:
    id: str
    numeric_id: int
    instance: int
    version: Version
    feature_bits: tuple[tuple[str, int], ...]
    limits: tuple[tuple[str, int, int], ...]
    flag: str
    affinity: int | None
    resources: tuple[str, ...]
    routes: tuple[str, ...]
    provider_source: str
    provider_symbol: str
    max_leases: int

    @property
    def features(self) -> tuple[str, ...]:
        return tuple(name for name, _ in self.feature_bits)

    @property
    def feature_mask(self) -> int:
        return sum(1 << bit for _, bit in self.feature_bits)


@dataclass(frozen=True)
class Composition:
    board: Board
    capabilities: tuple[Capability, ...]
    absences: tuple[dict[str, object], ...]
    grants: Mapping[str, tuple[CapabilityRequest, ...]]
    disabled_apps: frozenset[str]
    disabled_capabilities: frozenset[str]
    exclusions: tuple[dict[str, object], ...]
    required_consumer_exclusions: tuple[dict[str, object], ...]
    optional_fallbacks: tuple[dict[str, object], ...]


def canonical_json_bytes(document: object) -> bytes:
    return (
        json.dumps(document, ensure_ascii=False, allow_nan=False,
                   sort_keys=True, indent=2, separators=(",", ": "))
        .encode("utf-8") + b"\n"
    )


def _load_toml(path: Path) -> dict[str, Any]:
    try:
        with path.open("rb") as source:
            value = tomllib.load(source)
    except (OSError, tomllib.TOMLDecodeError) as exc:
        raise CapabilityError(f"cannot read {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise CapabilityError(f"{path}: root must be a table")
    return value


def _strict(value: Mapping[str, Any], fields: set[str], label: str) -> None:
    unknown = sorted(set(value) - fields)
    missing = sorted(fields - set(value))
    if unknown or missing:
        detail = []
        if unknown:
            detail.append("unknown=" + ",".join(unknown))
        if missing:
            detail.append("missing=" + ",".join(missing))
        raise CapabilityError(f"{label}: " + "; ".join(detail))


def _string(value: Any, label: str, pattern: re.Pattern[str] | None = None) -> str:
    if not isinstance(value, str) or not value:
        raise CapabilityError(f"{label}: expected non-empty string")
    if pattern is not None and pattern.fullmatch(value) is None:
        raise CapabilityError(f"{label}: invalid value {value!r}")
    return value


def _string_array(value: Any, label: str,
                  pattern: re.Pattern[str] | None = None) -> tuple[str, ...]:
    if not isinstance(value, list):
        raise CapabilityError(f"{label}: expected array")
    result = tuple(_string(item, label, pattern) for item in value)
    if len(result) != len(set(result)):
        raise CapabilityError(f"{label}: duplicate value")
    return result


def parse_version(value: Any, label: str) -> Version:
    text = _string(value, label)
    match = re.fullmatch(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", text)
    if match is None:
        raise CapabilityError(f"{label}: expected canonical MAJOR.MINOR.PATCH")
    numbers = tuple(int(part) for part in match.groups())
    if any(part > 0xFFFF for part in numbers):
        raise CapabilityError(f"{label}: component exceeds uint16")
    return Version(*numbers)


def _integer(value: Any, label: str, minimum: int, maximum: int) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise CapabilityError(f"{label}: expected integer")
    if value < minimum or value > maximum:
        raise CapabilityError(f"{label}: outside {minimum}..{maximum}")
    return value


def _request(value: Any, label: str, *, optional: bool) -> CapabilityRequest:
    if not isinstance(value, dict):
        raise CapabilityError(f"{label}: expected table")
    _strict(value, OPTIONAL_REQUEST_FIELDS if optional else REQUEST_FIELDS, label)
    minimum = parse_version(value["minimum"], f"{label}.minimum")
    maximum = parse_version(value["maximum_exclusive"], f"{label}.maximum_exclusive")
    if minimum >= maximum:
        raise CapabilityError(f"{label}: minimum must precede maximum_exclusive")
    fallback = None
    if optional:
        fallback = _string(value["fallback"], f"{label}.fallback", IDENTIFIER_RE)
    return CapabilityRequest(
        id=_string(value["id"], f"{label}.id", CAPABILITY_ID_RE),
        instance=_integer(value["instance"], f"{label}.instance", 0, 0xFFFF),
        minimum=minimum,
        maximum_exclusive=maximum,
        features=_string_array(value["features"], f"{label}.features", IDENTIFIER_RE),
        fallback=fallback,
    )


def _request_array(value: Any, label: str, *, optional: bool) -> tuple[CapabilityRequest, ...]:
    if not isinstance(value, list):
        raise CapabilityError(f"{label}: expected array of tables")
    result = tuple(_request(item, f"{label}[{index}]", optional=optional)
                   for index, item in enumerate(value))
    keys = [(item.id, item.instance) for item in result]
    if len(keys) != len(set(keys)):
        raise CapabilityError(f"{label}: duplicate capability request")
    return result


def load_app_requirements(path: Path = APP_REQUIREMENTS_PATH,
                          expected_apps: set[str] | None = None) -> dict[str, Requirements]:
    raw = _load_toml(path)
    _strict(raw, {"schema", "apps"}, "app requirements")
    if raw["schema"] != 2 or isinstance(raw["schema"], bool):
        raise CapabilityError("app requirements.schema: expected integer 2")
    apps = raw["apps"]
    if not isinstance(apps, dict):
        raise CapabilityError("app requirements.apps: expected table")
    if expected_apps is not None and set(apps) != expected_apps:
        raise CapabilityError("app requirements: apps must exactly match build app modules")
    result: dict[str, Requirements] = {}
    for app, table in apps.items():
        _string(app, "app requirements key", IDENTIFIER_RE)
        if not isinstance(table, dict):
            raise CapabilityError(f"app requirements {app}: expected table")
        _strict(table, {"requires", "required_capabilities", "optional_capabilities"},
                f"app requirements {app}")
        required = _request_array(table["required_capabilities"],
                                  f"app requirements {app}.required_capabilities",
                                  optional=False)
        optional = _request_array(table["optional_capabilities"],
                                  f"app requirements {app}.optional_capabilities",
                                  optional=True)
        overlap = {(item.id, item.instance) for item in required} & {
            (item.id, item.instance) for item in optional
        }
        if overlap:
            raise CapabilityError(f"app requirements {app}: request is both required and optional")
        if len(required) + len(optional) > 16:
            raise CapabilityError(
                f"app requirements {app}: exceeds fixed 16-grant owner capacity"
            )
        result[app] = Requirements(
            legacy=_string_array(table["requires"],
                                 f"app requirements {app}.requires", IDENTIFIER_RE),
            required=required,
            optional=optional,
        )
    return result


def load_consumer_requirements(path: Path = CONSUMER_REQUIREMENTS_PATH) -> dict[str, Requirements]:
    raw = _load_toml(path)
    _strict(raw, {"schema", "consumers"}, "capability consumers")
    if raw["schema"] != 1 or isinstance(raw["schema"], bool):
        raise CapabilityError("capability consumers.schema: expected integer 1")
    consumers = raw["consumers"]
    if not isinstance(consumers, dict) or not consumers:
        raise CapabilityError("capability consumers.consumers: expected non-empty table")
    result: dict[str, Requirements] = {}
    for name, table in consumers.items():
        _string(name, "capability consumer key", IDENTIFIER_RE)
        if not isinstance(table, dict):
            raise CapabilityError(f"capability consumer {name}: expected table")
        _strict(table, {
            "kind", "enabled_by_app", "required_capabilities",
            "optional_capabilities",
        } if "enabled_by_app" in table else {
            "kind", "required_capabilities", "optional_capabilities",
        },
                f"capability consumer {name}")
        kind = _string(table["kind"], f"capability consumer {name}.kind", IDENTIFIER_RE)
        if kind not in {"runtime", "service", "adapter"}:
            raise CapabilityError(f"capability consumer {name}.kind: unsupported {kind!r}")
        required = _request_array(
            table["required_capabilities"],
            f"capability consumer {name}.required_capabilities", optional=False)
        optional = _request_array(
            table["optional_capabilities"],
            f"capability consumer {name}.optional_capabilities", optional=True)
        if {(item.id, item.instance) for item in required} & {
            (item.id, item.instance) for item in optional
        }:
            raise CapabilityError(
                f"capability consumer {name}: request is both required and optional"
            )
        if len(required) + len(optional) > 16:
            raise CapabilityError(
                f"capability consumer {name}: exceeds fixed 16-grant owner capacity"
            )
        result[f"consumer:{name}"] = Requirements(
            legacy=(),
            required=required,
            optional=optional,
            enabled_by_app=(
                _string(table["enabled_by_app"],
                        f"capability consumer {name}.enabled_by_app", IDENTIFIER_RE)
                if "enabled_by_app" in table else None
            ),
        )
    return result


def load_catalog(path: Path = CATALOG_PATH, *, root: Path = ROOT) -> tuple[Capability, ...]:
    raw = _load_toml(path)
    _strict(raw, {"schema", "platform", "capabilities"}, "capability catalog")
    if raw["schema"] != 1 or isinstance(raw["schema"], bool):
        raise CapabilityError("capability catalog.schema: expected integer 1")
    _string(raw["platform"], "capability catalog.platform", IDENTIFIER_RE)
    entries = raw["capabilities"]
    if not isinstance(entries, list) or not entries:
        raise CapabilityError("capability catalog.capabilities: expected non-empty array")
    result: list[Capability] = []
    exact_fields = {
        "id", "numeric_id", "instance", "version", "feature_bits", "limits", "flags",
        "affinity", "resources", "routes", "provider_source", "provider_symbol",
        "max_leases",
    }
    for index, entry in enumerate(entries):
        label = f"capability catalog.capabilities[{index}]"
        if not isinstance(entry, dict):
            raise CapabilityError(f"{label}: expected table")
        _strict(entry, exact_fields, label)
        feature_bits = entry["feature_bits"]
        if not isinstance(feature_bits, dict):
            raise CapabilityError(f"{label}.feature_bits: expected table")
        normalized_bits: list[tuple[str, int]] = []
        for name, bit in feature_bits.items():
            normalized_bits.append((
                _string(name, f"{label}.feature_bits key", IDENTIFIER_RE),
                _integer(bit, f"{label}.feature_bits.{name}", 0, 63),
            ))
        if len({bit for _, bit in normalized_bits}) != len(normalized_bits):
            raise CapabilityError(f"{label}.feature_bits: duplicate bit")
        limits = entry["limits"]
        if not isinstance(limits, dict):
            raise CapabilityError(f"{label}.limits: expected table")
        normalized_limits: list[tuple[str, int, int]] = []
        for name, limit in limits.items():
            limit_label = f"{label}.limits.{name}"
            _string(name, f"{label}.limits key", IDENTIFIER_RE)
            if not isinstance(limit, dict):
                raise CapabilityError(f"{limit_label}: expected table")
            _strict(limit, {"key", "value"}, limit_label)
            normalized_limits.append((
                name,
                _integer(limit["key"], f"{limit_label}.key", 1, 0xFFFFFFFF),
                _integer(
                    limit["value"], f"{limit_label}.value",
                    0, 0xFFFFFFFFFFFFFFFF,
                ),
            ))
        if len({item[1] for item in normalized_limits}) != len(normalized_limits):
            raise CapabilityError(f"{label}.limits: duplicate numeric key")
        flags = _string_array(entry["flags"], f"{label}.flags", IDENTIFIER_RE)
        if len(flags) != 1 or flags[0] not in {"shared", "exclusive"}:
            raise CapabilityError(f"{label}.flags: expected exactly shared or exclusive")
        affinity_raw = entry["affinity"]
        affinity = None if affinity_raw == "any" else _integer(
            affinity_raw, f"{label}.affinity", 0, 0xFFFE
        )
        source = _string(entry["provider_source"], f"{label}.provider_source", SOURCE_RE)
        try:
            (root / source).resolve().relative_to(root.resolve())
        except ValueError as exc:
            raise CapabilityError(f"{label}.provider_source: escapes repository") from exc
        result.append(Capability(
            id=_string(entry["id"], f"{label}.id", CAPABILITY_ID_RE),
            numeric_id=_integer(entry["numeric_id"], f"{label}.numeric_id", 1, 0xFFFFFFFF),
            instance=_integer(entry["instance"], f"{label}.instance", 0, 0xFFFF),
            version=parse_version(entry["version"], f"{label}.version"),
            feature_bits=tuple(sorted(normalized_bits, key=lambda item: item[1])),
            limits=tuple(sorted(normalized_limits, key=lambda item: item[1])),
            flag=flags[0],
            affinity=affinity,
            resources=_string_array(entry["resources"], f"{label}.resources", IDENTIFIER_RE),
            routes=_string_array(entry["routes"], f"{label}.routes", IDENTIFIER_RE),
            provider_source=source,
            provider_symbol=_string(entry["provider_symbol"], f"{label}.provider_symbol", C_SYMBOL_RE),
            max_leases=_integer(entry["max_leases"], f"{label}.max_leases", 1, 0xFFFF),
        ))
    keys = [(item.numeric_id, item.instance) for item in result]
    names = [(item.id, item.instance) for item in result]
    symbols = [item.provider_symbol for item in result]
    if len(keys) != len(set(keys)):
        raise CapabilityError("capability catalog: duplicate numeric ID/instance")
    if len(names) != len(set(names)):
        raise CapabilityError("capability catalog: duplicate canonical ID/instance")
    if len(symbols) != len(set(symbols)):
        raise CapabilityError("capability catalog: duplicate provider symbol")
    return tuple(sorted(result, key=lambda item: (item.numeric_id, item.instance)))


def _absence(capability: Capability, code: str, detail: list[str]) -> dict[str, object]:
    if code not in ABSENCE_CODES:
        raise AssertionError(code)
    return {
        "id": capability.id,
        "instance": capability.instance,
        "code": code,
        "detail": sorted(detail),
    }


def capability_availability(
    board: Board, catalog: tuple[Capability, ...], disabled_capabilities: set[str],
    *, root: Path = ROOT,
) -> tuple[tuple[Capability, ...], tuple[dict[str, object], ...]]:
    known = {item.id for item in catalog}
    unknown = sorted(disabled_capabilities - known)
    if unknown:
        raise CapabilityError("unknown disabled capability: " + ", ".join(unknown))
    board_kinds = {item["kind"] for item in board.data["devices"]}
    driver_kinds = board.driver_supported_kinds()
    selected_routes = {item["id"] for item in board.selected_routes()}
    available: list[Capability] = []
    absences: list[dict[str, object]] = []
    for capability in catalog:
        missing_resources = sorted(set(capability.resources) - board_kinds)
        unsupported = sorted(set(capability.resources) - driver_kinds)
        missing_routes = sorted(set(capability.routes) - selected_routes)
        source = root / capability.provider_source
        if capability.id in disabled_capabilities:
            absences.append(_absence(capability, "provider-excluded", ["disabled-by-build"]))
        elif missing_resources:
            absences.append(_absence(capability, "resource-absent", missing_resources))
        elif unsupported:
            absences.append(_absence(capability, "driver-unsupported", unsupported))
        elif missing_routes:
            absences.append(_absence(capability, "route-unavailable", missing_routes))
        elif not source.is_file():
            absences.append(_absence(capability, "provider-excluded", [capability.provider_source]))
        else:
            source_text = source.read_text(encoding="utf-8")
            definition = re.compile(
                rf"\bconst\s+hk_capability_provider_t\s+"
                rf"{re.escape(capability.provider_symbol)}\s*=\s*\{{"
                rf"(?P<body>.*?)\}}\s*;",
                re.DOTALL,
            )
            match = definition.search(source_text)
            if match is None:
                raise CapabilityError(
                    f"{capability.provider_source}: provider symbol "
                    f"{capability.provider_symbol!r} has no const object definition"
                )
            max_leases = re.compile(
                rf"\.max_leases\s*=\s*{capability.max_leases}U?\b"
            )
            if max_leases.search(match.group("body")) is None:
                raise CapabilityError(
                    f"{capability.provider_source}: provider "
                    f"{capability.provider_symbol!r} does not declare mapped "
                    f"max_leases={capability.max_leases}"
                )
            available.append(capability)
    return tuple(available), tuple(absences)


def _match_request(
    request: CapabilityRequest,
    available: Mapping[tuple[str, int], Capability],
    absent: Mapping[tuple[str, int], dict[str, object]],
) -> tuple[Capability | None, dict[str, object] | None]:
    key = (request.id, request.instance)
    capability = available.get(key)
    if capability is None:
        reason = absent.get(key)
        if reason is None:
            reason = {
                "id": request.id, "instance": request.instance,
                "code": "resource-absent", "detail": ["unmapped-capability"],
            }
        return None, reason
    if not (request.minimum <= capability.version < request.maximum_exclusive):
        return None, _absence(capability, "version-incompatible", [
            f"available={capability.version}",
            f"requested=[{request.minimum},{request.maximum_exclusive})",
        ])
    missing = sorted(set(request.features) - set(capability.features))
    if missing:
        return None, _absence(capability, "feature-missing", missing)
    return capability, None


def compose(
    board: Board,
    app_ids: set[str],
    disabled_apps: set[str],
    required_apps: set[str],
    disabled_capabilities: set[str],
    *,
    allow_required_consumer_exclusion: bool = False,
    app_requirements_path: Path = APP_REQUIREMENTS_PATH,
    consumer_requirements_path: Path = CONSUMER_REQUIREMENTS_PATH,
    catalog_path: Path = CATALOG_PATH,
    root: Path = ROOT,
) -> Composition:
    unknown_required = sorted(required_apps - app_ids)
    if unknown_required:
        raise CapabilityError("unknown required app(s): " + ", ".join(unknown_required))
    unknown_disabled = sorted(disabled_apps - app_ids)
    if unknown_disabled:
        raise CapabilityError("unknown disabled app(s): " + ", ".join(unknown_disabled))
    conflict = sorted(required_apps & disabled_apps)
    if conflict:
        raise CapabilityError("app is both disabled and required: " + ", ".join(conflict))
    apps = load_app_requirements(app_requirements_path, app_ids)
    consumers = load_consumer_requirements(consumer_requirements_path)
    catalog = load_catalog(catalog_path, root=root)
    known_requests = {(item.id, item.instance) for item in catalog}
    for owner, requirements in {**apps, **consumers}.items():
        for request in requirements.required + requirements.optional:
            if (request.id, request.instance) not in known_requests:
                raise CapabilityError(
                    f"{owner}: unknown capability ID/instance "
                    f"{request.id}[{request.instance}]"
                )
        if (requirements.enabled_by_app is not None and
                requirements.enabled_by_app not in app_ids):
            raise CapabilityError(
                f"{owner}: enabled_by_app names unknown app "
                f"{requirements.enabled_by_app!r}"
            )
    if board.registry.platform != _load_toml(catalog_path)["platform"]:
        raise CapabilityError(
            f"capability catalog platform does not match board {board.registry.platform!r}"
        )
    available, absences = capability_availability(
        board, catalog, disabled_capabilities, root=root
    )
    available_by_key = {(item.id, item.instance): item for item in available}
    absent_by_key = {(str(item["id"]), int(item["instance"])): item for item in absences}
    composed_disabled = set(disabled_apps)
    exclusions: list[dict[str, object]] = []
    consumer_exclusions: list[dict[str, object]] = []
    fallbacks: list[dict[str, object]] = []
    grants: dict[str, tuple[CapabilityRequest, ...]] = {}

    def evaluate(name: str, requirements: Requirements, *, app: bool) -> None:
        legacy_missing = sorted(set(requirements.legacy) - board.driver_supported_kinds())
        matched: list[CapabilityRequest] = []
        failures: list[dict[str, object]] = []
        if legacy_missing:
            failures.append({"code": "driver-unsupported", "missing": legacy_missing})
        for request in requirements.required:
            capability, reason = _match_request(request, available_by_key, absent_by_key)
            if capability is None:
                assert reason is not None
                failures.append({"code": reason["code"], "missing": [request.id]})
            else:
                matched.append(request)
        for request in requirements.optional:
            capability, reason = _match_request(request, available_by_key, absent_by_key)
            if capability is None:
                assert reason is not None and request.fallback is not None
                fallbacks.append({
                    "consumer": name,
                    "id": request.id,
                    "instance": request.instance,
                    "fallback": request.fallback,
                    "reason": reason["code"],
                })
            else:
                matched.append(request)
        if failures:
            if not app:
                if allow_required_consumer_exclusion:
                    first = failures[0]
                    consumer_exclusions.append({
                        "consumer": name,
                        "code": first["code"],
                        "missing": sorted({
                            str(item) for failure in failures
                            for item in failure["missing"]
                        }),
                    })
                    return
                first = failures[0]
                raise CapabilityError(
                    f"required capability consumer {name!r} is unavailable: "
                    + ", ".join(str(item) for item in first["missing"])
                )
            if name in required_apps:
                raise CapabilityError(
                    f"required app {name!r} is unavailable on {board.id}: "
                    + ", ".join(str(item) for failure in failures
                                for item in failure["missing"])
                )
            composed_disabled.add(name)
            first = failures[0]
            exclusions.append({
                "app": name,
                "code": first["code"],
                "missing": sorted({
                    str(item) for failure in failures for item in failure["missing"]
                }),
            })
            return
        if name not in composed_disabled:
            grants[name] = tuple(matched)

    for app in sorted(app_ids):
        if app not in composed_disabled:
            evaluate(app, apps[app], app=True)
    for consumer in sorted(consumers):
        requirements = consumers[consumer]
        if (requirements.enabled_by_app is None or
                requirements.enabled_by_app not in composed_disabled):
            evaluate(consumer, requirements, app=False)
    return Composition(
        board=board,
        capabilities=available,
        absences=absences,
        grants=grants,
        disabled_apps=frozenset(composed_disabled),
        disabled_capabilities=frozenset(disabled_capabilities),
        exclusions=tuple(sorted(exclusions, key=lambda item: str(item["app"]))),
        required_consumer_exclusions=tuple(sorted(
            consumer_exclusions, key=lambda item: str(item["consumer"])
        )),
        optional_fallbacks=tuple(sorted(
            fallbacks, key=lambda item: (str(item["consumer"]), str(item["id"]))
        )),
    )


def _request_document(request: CapabilityRequest) -> dict[str, object]:
    return {
        "id": request.id,
        "instance": request.instance,
        "minimum": str(request.minimum),
        "maximum_exclusive": str(request.maximum_exclusive),
        "features": list(request.features),
    }


def capabilities_document(composition: Composition) -> dict[str, object]:
    entries = []
    for item in composition.capabilities:
        entries.append({
            "id": item.id,
            "numeric_id": item.numeric_id,
            "instance": item.instance,
            "version": str(item.version),
            "features": list(item.features),
            "feature_mask": item.feature_mask,
            "flags": [item.flag],
            "affinity": "any" if item.affinity is None else item.affinity,
            "provider_symbol": item.provider_symbol,
            "max_leases": item.max_leases,
            "limits": [
                {"name": name, "key": key, "value": value}
                for name, key, value in item.limits
            ],
        })
    return {
        "schema": 1,
        "board": composition.board.id,
        "platform": composition.board.registry.platform,
        "support": composition.board.support,
        "runtime_supported": composition.board.support == "runtime",
        "entries": entries,
        "absences": list(composition.absences),
    }


def composition_document(composition: Composition, capabilities_sha256: str) -> dict[str, object]:
    return {
        "schema": 2,
        "board": composition.board.id,
        "platform": composition.board.registry.platform,
        "support": composition.board.support,
        "runtime_supported": composition.board.support == "runtime",
        "disabled_apps": sorted(composition.disabled_apps),
        "disabled_capabilities": sorted(composition.disabled_capabilities),
        "exclusions": list(composition.exclusions),
        "required_consumer_exclusions": list(
            composition.required_consumer_exclusions
        ),
        "optional_fallbacks": list(composition.optional_fallbacks),
        "capabilities": {
            "path": "capabilities.json",
            "sha256": capabilities_sha256,
            "entry_count": len(composition.capabilities),
        },
        "grants": {
            consumer: [_request_document(request) for request in requests]
            for consumer, requests in sorted(composition.grants.items())
        },
    }


def write_artifacts(composition: Composition, output_dir: Path) -> tuple[Path, Path]:
    output_dir.mkdir(parents=True, exist_ok=True)
    capabilities = canonical_json_bytes(capabilities_document(composition))
    capabilities_path = output_dir / "capabilities.json"
    capabilities_path.write_bytes(capabilities)
    digest = hashlib.sha256(capabilities).hexdigest()
    composition_path = output_dir / "composition.json"
    composition_path.write_bytes(canonical_json_bytes(
        composition_document(composition, digest)
    ))
    return capabilities_path, composition_path


def _c_request(request: CapabilityRequest, catalog: Mapping[tuple[str, int], Capability]) -> str:
    capability = catalog[(request.id, request.instance)]
    mask = sum(1 << dict(capability.feature_bits)[name] for name in request.features)
    return (
        "    {.request = {"
        f"sizeof(hk_capability_request_t), HK_CAPABILITY_REQUEST_VERSION, "
        f"0x{capability.numeric_id:08X}U, {request.minimum.c_fields()}, "
        f"{request.maximum_exclusive.c_fields()}, UINT64_C(0x{mask:016X}), "
        f"{request.instance}U, 0U"
        "}},"
    )


def generated_c(composition: Composition) -> str:
    lines = [
        "/* Generated by tools/gen_capability_inventory.py; do not edit. */",
        "#include \"capability_inventory_binding.h\"",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "#include <string.h>",
        "",
    ]
    if composition.capabilities:
        for item in composition.capabilities:
            lines.append(f"extern const hk_capability_provider_t {item.provider_symbol};")
        lines.append("")
        for index, item in enumerate(composition.capabilities):
            if not item.limits:
                continue
            lines.append(f"static const hk_capability_limit_t s_limits_{index}[] = {{")
            for _, key, value in item.limits:
                lines.append(
                    "    {sizeof(hk_capability_limit_t), "
                    f"HK_CAPABILITY_LIMIT_VERSION, {key}U, UINT64_C({value})}},"
                )
            lines.extend(["};", ""])
        lines.append("static const hk_capability_info_t s_inventory[] = {")
        for index, item in enumerate(composition.capabilities):
            flag = "HK_CAPABILITY_FLAG_SHARED" if item.flag == "shared" else "HK_CAPABILITY_FLAG_EXCLUSIVE"
            affinity = "HK_CAPABILITY_CORE_ANY" if item.affinity is None else f"{item.affinity}U"
            limits = f"s_limits_{index}" if item.limits else "NULL"
            lines.append(
                "    {sizeof(hk_capability_info_t), HK_CAPABILITY_INFO_VERSION, "
                f"0x{item.numeric_id:08X}U, {item.version.c_fields()}, "
                f"UINT64_C(0x{item.feature_mask:016X}), {flag}, {item.instance}U, "
                f"{affinity}, {limits}, {len(item.limits)}U, 0U}},"
            )
        lines.extend(["};", "", "static const hk_capability_provider_t *const s_providers[] = {"])
        lines.extend(f"    &{item.provider_symbol}," for item in composition.capabilities)
        lines.extend(["};", ""])
    catalog = {(item.id, item.instance): item for item in composition.capabilities}
    grant_names: dict[str, str] = {}
    for index, (consumer, requests) in enumerate(sorted(composition.grants.items())):
        if not requests:
            continue
        symbol = f"s_grants_{index}"
        grant_names[consumer] = symbol
        lines.append(f"static const hk_capability_grant_t {symbol}[] = {{")
        lines.extend(_c_request(request, catalog) for request in requests)
        lines.extend(["};", ""])
    lines.extend([
        "void hk_generated_capability_inventory_get(",
        "    const hk_capability_info_t **inventory,",
        "    const hk_capability_provider_t *const **providers,",
        "    uint16_t *count)",
        "{",
    ])
    if composition.capabilities:
        lines.extend([
            "    if(inventory)", "        *inventory = s_inventory;",
            "    if(providers)", "        *providers = s_providers;",
            f"    if(count)", f"        *count = {len(composition.capabilities)}U;",
        ])
    else:
        lines.extend([
            "    if(inventory)", "        *inventory = NULL;",
            "    if(providers)", "        *providers = NULL;",
            "    if(count)", "        *count = 0U;",
        ])
    lines.extend(["}", "", "const hk_capability_grant_t *hk_generated_capability_grants_for(",
                  "    const char *consumer_id, uint16_t *count)", "{",
                  "    if(count)", "        *count = 0U;", "    if(!consumer_id || !count)",
                  "        return NULL;"])
    if grant_names:
        for consumer, symbol in grant_names.items():
            lines.extend([
                f"    if(strcmp(consumer_id, \"{consumer}\") == 0)", "    {",
                f"        *count = (uint16_t)(sizeof({symbol}) / sizeof({symbol}[0]));",
                f"        return {symbol};", "    }",
            ])
    else:
        lines.append("    (void)consumer_id;")
    lines.extend(["    return NULL;", "}", ""])
    return "\n".join(lines)


def write_generated_c(composition: Composition, stage: Path) -> Path:
    path = stage / "firmware" / "src" / "capabilities" / "capability_inventory_generated.c"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(generated_c(composition), encoding="utf-8", newline="\n")
    return path


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board", required=True)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--disable-app", action="append", default=[])
    parser.add_argument("--require-app", action="append", default=[])
    parser.add_argument("--disable-capability", action="append", default=[])
    args = parser.parse_args(argv)
    try:
        board = load_board(args.board)
        apps = set(load_app_requirements())
        result = compose(
            board, apps, set(args.disable_app), set(args.require_app),
            set(args.disable_capability),
        )
        output = args.output_dir or ROOT / "build" / board.id
        capabilities_path, composition_path = write_artifacts(result, output)
        print(f"[OK] {capabilities_path}")
        print(f"[OK] {composition_path}")
        return 0
    except (CapabilityError, ContractError, OSError, ValueError) as exc:
        print(f"[ERR] {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
