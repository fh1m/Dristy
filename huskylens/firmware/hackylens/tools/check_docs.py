#!/usr/bin/env python3
"""Validate HackyLens documentation governance contracts."""

from __future__ import annotations

from datetime import date
from pathlib import Path
import re
import sys
from typing import Iterable, NamedTuple
from urllib.parse import unquote


ROOT = Path(__file__).resolve().parents[1]

SEMVER_RE = re.compile(
    r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)"
    r"(?:-[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?"
    r"(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$"
)
RELEASE_SEMVER_RE = re.compile(r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$")
CONTRACT_ID_RE = re.compile(r"^[a-z][a-z0-9]*(?:[.-][a-z0-9]+)*$")
OWNER_RE = re.compile(r"^[a-z][a-z0-9]*(?:-[a-z0-9]+)*$")
ENCODED_MAJOR_RE = re.compile(r"^(0|[1-9]\d*)$")
ADR_FILE_RE = re.compile(r"^(\d{4})-[a-z0-9]+(?:-[a-z0-9]+)*\.md$")
MARKDOWN_LINK_RE = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")
REFERENCE_LINK_RE = re.compile(r"!?\[([^\]]*)\]\[([^\]]*)\]")
REFERENCE_DEFINITION_RE = re.compile(r"^\s{0,3}\[([^\]]+)\]:\s*(.+?)\s*$")
ADR_REFERENCE_RE = re.compile(r"^\d{4}$")
HEADING_RE = re.compile(r"^\s{0,3}(#{1,6})\s+(.+?)\s*#*\s*$")

STABILITIES = {"experimental", "stable", "deprecated"}

TECHNICAL_CONTRACTS = (
    Path("docs/HMPY_PROTOCOL.md"),
    Path("docs/MICROPYTHON_API.md"),
    Path("docs/EXTERNAL_LINK_PROTOCOL.md"),
    Path("docs/APP_LIFECYCLE.md"),
    Path("docs/AI_MODELS.md"),
)

ENTRY_DOCUMENTS = (
    Path("README.md"),
    Path("docs/ARCHITECTURE.md"),
    Path("docs/ARCHITECTURE_VISION.md"),
    Path("docs/CURRENT_STATE.md"),
    Path("docs/ROADMAP.md"),
    Path("docs/MODULES.md"),
)

CLAIM_SURFACES = (
    Path("README.md"),
    Path("docs/ARCHITECTURE.md"),
    Path("docs/MODULES.md"),
)

PREVIEW_MARKER = (
    "hackylens v0.4 is a layered k210 reference firmware and micropython "
    "technology preview"
)

FORBIDDEN_CLAIMS = (
    "openmv-class",
    "hardware-independent platform",
    "hardware-independent robotics platform",
    "open application standard",
)

CLAIM_QUALIFIERS = (
    "candidate",
    "target",
    "roadmap goal",
    "not yet",
    "does not yet",
    "not demonstrated",
)

ADR_SECTIONS = (
    "Context",
    "Decision",
    "Alternatives",
    "Consequences",
    "Compatibility and Migration",
    "Evidence",
    "References",
)


class Issue(NamedTuple):
    path: Path
    line: int
    message: str


class FrontMatter(NamedTuple):
    values: dict[str, str]
    lines: dict[str, int]


def relative_path(root: Path, path: Path) -> Path:
    try:
        return path.resolve().relative_to(root.resolve())
    except ValueError:
        return path


def issue(root: Path, path: Path, line: int, message: str) -> Issue:
    return Issue(relative_path(root, path), line, message)


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def parse_front_matter(path: Path) -> FrontMatter:
    lines = read_text(path).splitlines()
    if not lines or lines[0].strip() != "---":
        return FrontMatter({}, {})

    values: dict[str, str] = {}
    key_lines: dict[str, int] = {}
    for index, source in enumerate(lines[1:], start=2):
        if source.strip() == "---":
            return FrontMatter(values, key_lines)
        match = re.fullmatch(r"([a-z][a-z0-9-]*):(?:\s*(.*))?", source)
        if match:
            key = match.group(1)
            values[key] = (match.group(2) or "").strip()
            key_lines[key] = index
    return FrontMatter({}, {})


def contract_paths(root: Path) -> list[Path]:
    specs = sorted(
        path
        for path in (root / "docs" / "spec").rglob("*.md")
        if path.name != "README.md"
    )
    return [
        root / "docs" / "ARCHITECTURE_VISION.md",
        *specs,
        *(root / path for path in TECHNICAL_CONTRACTS),
    ]


def validate_contract_documents(
    root: Path, paths: Iterable[Path]
) -> tuple[list[Issue], dict[str, tuple[Path, FrontMatter]]]:
    issues: list[Issue] = []
    contracts: dict[str, tuple[Path, FrontMatter]] = {}

    for path in paths:
        if not path.is_file():
            issues.append(issue(root, path, 1, "required contract document is missing"))
            continue

        front = parse_front_matter(path)
        required = ("contract-id", "owner", "version", "stability")
        for key in required:
            if not front.values.get(key):
                issues.append(issue(root, path, 1, f"missing front matter field {key!r}"))

        contract_id = front.values.get("contract-id", "")
        owner = front.values.get("owner", "")
        version = front.values.get("version", "")
        stability = front.values.get("stability", "")

        if contract_id and not CONTRACT_ID_RE.fullmatch(contract_id):
            issues.append(
                issue(
                    root,
                    path,
                    front.lines.get("contract-id", 1),
                    f"invalid contract-id {contract_id!r}",
                )
            )
        if owner and not OWNER_RE.fullmatch(owner):
            issues.append(
                issue(
                    root,
                    path,
                    front.lines.get("owner", 1),
                    f"invalid logical owner {owner!r}",
                )
            )
        if version and not SEMVER_RE.fullmatch(version):
            issues.append(
                issue(
                    root,
                    path,
                    front.lines.get("version", 1),
                    f"invalid semantic version {version!r}",
                )
            )
        if stability and stability not in STABILITIES:
            issues.append(
                issue(
                    root,
                    path,
                    front.lines.get("stability", 1),
                    f"invalid stability {stability!r}",
                )
            )

        if stability == "deprecated":
            deprecated_since = front.values.get("deprecated-since", "")
            removal_version = front.values.get("removal-version", "")
            migration_guide = front.values.get("migration-guide", "")
            replacement_contract = front.values.get("replacement-contract", "")
            if not deprecated_since:
                issues.append(
                    issue(root, path, 1, "deprecated contract lacks deprecated-since")
                )
            if not removal_version:
                issues.append(
                    issue(root, path, 1, "deprecated contract lacks removal-version")
                )
            if not migration_guide and not replacement_contract:
                issues.append(
                    issue(
                        root,
                        path,
                        1,
                        "deprecated contract lacks migration-guide or replacement-contract",
                    )
                )
            if deprecated_since and not RELEASE_SEMVER_RE.fullmatch(deprecated_since):
                issues.append(
                    issue(
                        root,
                        path,
                        front.lines.get("deprecated-since", 1),
                        "deprecated-since must be a release semantic version",
                    )
                )
            if removal_version and not RELEASE_SEMVER_RE.fullmatch(removal_version):
                issues.append(
                    issue(
                        root,
                        path,
                        front.lines.get("removal-version", 1),
                        "removal-version must be a release semantic version",
                    )
                )
            if RELEASE_SEMVER_RE.fullmatch(deprecated_since) and RELEASE_SEMVER_RE.fullmatch(
                removal_version
            ):
                deprecated_tuple = tuple(int(part) for part in deprecated_since.split("."))
                removal_tuple = tuple(int(part) for part in removal_version.split("."))
                minimum = (deprecated_tuple[0], deprecated_tuple[1] + 1, 0)
                if removal_tuple < minimum:
                    issues.append(
                        issue(
                            root,
                            path,
                            front.lines.get("removal-version", 1),
                            f"removal-version must be at least {minimum[0]}.{minimum[1]}.{minimum[2]}",
                        )
                    )

        if contract_id:
            if contract_id in contracts:
                first_path = relative_path(root, contracts[contract_id][0])
                issues.append(
                    issue(
                        root,
                        path,
                        front.lines.get("contract-id", 1),
                        f"duplicate contract-id {contract_id!r}; first declared in {first_path}",
                    )
                )
            else:
                contracts[contract_id] = (path, front)

    anchor_cache: dict[Path, set[str]] = {}
    for contract_id, (path, front) in contracts.items():
        if front.values.get("stability") != "deprecated":
            continue

        migration_guide = front.values.get("migration-guide", "")
        if migration_guide:
            line = front.lines.get("migration-guide", 1)
            lower = migration_guide.lower()
            if lower.startswith(("http://", "https://", "mailto:", "data:")):
                issues.append(
                    issue(
                        root,
                        path,
                        line,
                        "migration-guide must be a repository-local Markdown target",
                    )
                )
            else:
                decoded = unquote(migration_guide)
                location = decoded.partition("#")[0].split("?", 1)[0]
                destination = path if not location else (path.parent / location).resolve()
                if destination.suffix.lower() != ".md":
                    issues.append(
                        issue(
                            root,
                            path,
                            line,
                            "migration-guide must target a Markdown document",
                        )
                    )
                issues.extend(
                    check_link_target(
                        root, path, line, migration_guide, anchor_cache
                    )
                )

        replacement = front.values.get("replacement-contract", "")
        if replacement:
            line = front.lines.get("replacement-contract", 1)
            if not CONTRACT_ID_RE.fullmatch(replacement):
                issues.append(
                    issue(root, path, line, "replacement-contract must be a contract-id")
                )
            elif replacement == contract_id:
                issues.append(
                    issue(root, path, line, "replacement-contract cannot reference itself")
                )
            elif replacement not in contracts:
                issues.append(
                    issue(
                        root,
                        path,
                        line,
                        f"replacement-contract references unknown contract {replacement!r}",
                    )
                )
            elif contracts[replacement][1].values.get("stability") == "deprecated":
                issues.append(
                    issue(
                        root,
                        path,
                        line,
                        f"replacement-contract {replacement!r} is also deprecated",
                    )
                )

    return issues, contracts


def markdown_files(root: Path) -> list[Path]:
    files: set[Path] = set()
    for direct in (root / "README.md", root / "isp_stub" / "NOTICE.md"):
        if direct.is_file():
            files.add(direct)
    for directory in ("docs", "models", "sdcard", ".github"):
        base = root / directory
        if base.is_dir():
            files.update(base.rglob("*.md"))
    return sorted(files)


def lines_outside_fences(text: str) -> list[tuple[int, str]]:
    result: list[tuple[int, str]] = []
    fence: str | None = None
    for number, line in enumerate(text.splitlines(), start=1):
        stripped = line.lstrip()
        marker = stripped[:3]
        if marker in ("```", "~~~"):
            if fence is None:
                fence = marker
            elif fence == marker:
                fence = None
            continue
        if fence is None:
            result.append((number, line))
    return result


def heading_anchors(text: str) -> set[str]:
    anchors: set[str] = set()
    counts: dict[str, int] = {}
    for _, line in lines_outside_fences(text):
        match = HEADING_RE.match(line)
        if not match:
            continue
        heading = re.sub(r"[`*_~]", "", match.group(2)).lower()
        heading = re.sub(r"[^\w\- ]", "", heading, flags=re.UNICODE)
        base = re.sub(r"\s+", "-", heading.strip())
        count = counts.get(base, 0)
        anchor = base if count == 0 else f"{base}-{count}"
        counts[base] = count + 1
        anchors.add(anchor)
    return anchors


def link_target(raw: str) -> str:
    value = raw.strip()
    if value.startswith("<") and ">" in value:
        return value[1 : value.index(">")]
    match = re.match(r"([^\s]+)", value)
    return match.group(1) if match else value


def reference_label(raw: str) -> str:
    return re.sub(r"\s+", " ", raw.strip()).casefold()


def check_link_target(
    root: Path,
    path: Path,
    number: int,
    target: str,
    anchor_cache: dict[Path, set[str]],
) -> list[Issue]:
    lower = target.lower()
    if lower.startswith(("http://", "https://", "mailto:", "data:")):
        return []

    decoded = unquote(target)
    location, separator, fragment = decoded.partition("#")
    location = location.split("?", 1)[0]
    destination = path if not location else (path.parent / location).resolve()

    try:
        destination.relative_to(root.resolve())
    except ValueError:
        return [issue(root, path, number, f"local link escapes repository: {target!r}")]

    if not destination.exists():
        return [issue(root, path, number, f"broken local link: {target!r}")]

    if separator and fragment and destination.is_file():
        anchor = fragment.lower()
        if destination not in anchor_cache:
            anchor_cache[destination] = heading_anchors(read_text(destination))
        if anchor not in anchor_cache[destination]:
            return [issue(root, path, number, f"missing Markdown anchor: {target!r}")]
    return []


def check_links(root: Path, paths: Iterable[Path]) -> list[Issue]:
    issues: list[Issue] = []
    anchor_cache: dict[Path, set[str]] = {}

    for path in paths:
        text = read_text(path)
        source_lines = lines_outside_fences(text)
        definitions: dict[str, tuple[int, str]] = {}

        for number, line in source_lines:
            match = REFERENCE_DEFINITION_RE.match(line)
            if not match or match.group(1).startswith("^"):
                continue
            label = reference_label(match.group(1))
            target = link_target(match.group(2))
            if label in definitions:
                issues.append(
                    issue(root, path, number, f"duplicate Markdown reference definition: {label!r}")
                )
            else:
                definitions[label] = (number, target)

        for number, line in source_lines:
            for match in MARKDOWN_LINK_RE.finditer(line):
                target = link_target(match.group(1))
                issues.extend(check_link_target(root, path, number, target, anchor_cache))

            for match in REFERENCE_LINK_RE.finditer(line):
                label = reference_label(match.group(2) or match.group(1))
                if label not in definitions:
                    issues.append(
                        issue(root, path, number, f"missing Markdown reference definition: {label!r}")
                    )

        for number, target in definitions.values():
            issues.extend(check_link_target(root, path, number, target, anchor_cache))
    return issues


def plain_text(text: str) -> str:
    text = re.sub(r"(?m)^\s*>\s?", "", text)
    text = text.replace("`", "")
    return re.sub(r"\s+", " ", text).strip().lower()


def check_preview_markers(root: Path, paths: Iterable[Path]) -> list[Issue]:
    issues: list[Issue] = []
    for path in paths:
        if not path.is_file():
            issues.append(issue(root, path, 1, "required entry document is missing"))
            continue
        if PREVIEW_MARKER not in plain_text(read_text(path)):
            issues.append(issue(root, path, 1, "missing canonical v0.4 technology-preview marker"))
    return issues


def markdown_paragraphs(text: str) -> list[tuple[int, str]]:
    paragraphs: list[tuple[int, str]] = []
    current: list[str] = []
    start = 1
    for number, line in lines_outside_fences(text):
        if line.strip():
            if not current:
                start = number
            current.append(line.strip())
        elif current:
            paragraphs.append((start, " ".join(current)))
            current = []
    if current:
        paragraphs.append((start, " ".join(current)))
    return paragraphs


def check_forbidden_claims(root: Path, paths: Iterable[Path]) -> list[Issue]:
    issues: list[Issue] = []
    for path in paths:
        for number, paragraph in markdown_paragraphs(read_text(path)):
            lowered = paragraph.lower()
            claim = next((item for item in FORBIDDEN_CLAIMS if item in lowered), None)
            if claim and not any(item in lowered for item in CLAIM_QUALIFIERS):
                issues.append(
                    issue(root, path, number, f"unqualified forbidden claim {claim!r}")
                )
    return issues


def contract_encoded_major(
    root: Path,
    contracts: dict[str, tuple[Path, FrontMatter]],
    contract_id: str,
    field: str,
) -> tuple[int | None, list[Issue]]:
    if contract_id not in contracts:
        return None, [issue(root, root / "docs" / "spec" / "README.md", 1, f"missing contract {contract_id!r}")]
    path, front = contracts[contract_id]
    value = front.values.get(field, "")
    if not value:
        return None, [issue(root, path, 1, f"{contract_id} lacks {field}")]
    if not ENCODED_MAJOR_RE.fullmatch(value):
        return None, [
            issue(
                root,
                path,
                front.lines.get(field, 1),
                f"{field} must be a non-negative decimal integer",
            )
        ]
    return int(value), []


def fixed_constant(
    root: Path, relative: str, pattern: str, label: str
) -> tuple[int | None, list[Issue]]:
    path = root / relative
    if not path.is_file():
        return None, [issue(root, path, 1, f"canonical {label} source is missing")]
    text = read_text(path)
    match = re.search(pattern, text, flags=re.MULTILINE)
    if not match:
        return None, [issue(root, path, 1, f"canonical {label} constant is missing")]
    return int(match.group(1)), []


def check_canonical_versions(
    root: Path, contracts: dict[str, tuple[Path, FrontMatter]]
) -> list[Issue]:
    issues: list[Issue] = []
    version_path = root / "VERSION"
    if not version_path.is_file():
        issues.append(issue(root, version_path, 1, "canonical firmware VERSION is missing"))
    else:
        firmware_version = read_text(version_path).strip()
        if not RELEASE_SEMVER_RE.fullmatch(firmware_version):
            issues.append(issue(root, version_path, 1, "firmware VERSION is not release SemVer"))
        readme = root / "README.md"
        if readme.is_file():
            readme_text = read_text(readme)
            for expected in (
                f"Firmware version {firmware_version}",
                f"firmware-v{firmware_version}",
            ):
                if expected not in readme_text:
                    issues.append(issue(root, readme, 1, f"README badge does not contain {expected!r}"))

    mappings = (
        (
            "hackylens.hmpy",
            "wire-major",
            (
                ("tools/hmpy_protocol.py", r"^PROTOCOL_VERSION\s*=\s*(\d+)\s*$", "HMPY host version"),
                ("firmware/src/services/hmpy_codec.h", r"^#define\s+HMPY_PROTOCOL_VERSION\s+(\d+)U?\s*$", "HMPY firmware version"),
            ),
            root / "docs" / "HMPY_PROTOCOL.md",
            r"^# HackyLens MicroPython Protocol \(HMPY\) v(\d+)\s*$",
        ),
        (
            "hackylens.external-link",
            "wire-major",
            (
                ("firmware/src/services/external_link_protocol.h", r"^#define\s+HK_LINK_PROTOCOL_VERSION\s+(\d+)U?\s*$", "External Link version"),
            ),
            root / "docs" / "EXTERNAL_LINK_PROTOCOL.md",
            r"^# HackyLens External Link Protocol v(\d+)\s*$",
        ),
        (
            "hackylens.ai-model-package",
            "schema-major",
            (
                ("tools/ai_model.py", r"^MANIFEST_VERSION\s*=\s*(\d+)\s*$", "AI host manifest version"),
                ("firmware/src/storage/ai_model_storage.c", r"^#define\s+AI_MANIFEST_VERSION\s+(\d+)U?\s*$", "AI firmware manifest version"),
            ),
            None,
            None,
        ),
        (
            "hackylens.micropython-api",
            "api-major",
            (),
            root / "docs" / "MICROPYTHON_API.md",
            r"^# HackyLens MicroPython API v(\d+)\s*$",
        ),
    )

    for contract_id, encoded_field, constants, heading_path, heading_pattern in mappings:
        major, major_issues = contract_encoded_major(
            root, contracts, contract_id, encoded_field
        )
        issues.extend(major_issues)
        if major is None:
            continue
        for relative, pattern, label in constants:
            value, constant_issues = fixed_constant(root, relative, pattern, label)
            issues.extend(constant_issues)
            if value is not None and value != major:
                issues.append(
                    issue(
                        root,
                        root / relative,
                        1,
                        f"{label} {value} does not match {contract_id} {encoded_field} {major}",
                    )
                )
        if heading_path is not None and heading_pattern is not None:
            match = re.search(heading_pattern, read_text(heading_path), flags=re.MULTILINE)
            if not match:
                issues.append(issue(root, heading_path, 1, "versioned contract heading is missing"))
            elif int(match.group(1)) != major:
                issues.append(
                    issue(root, heading_path, 1,
                          f"heading major does not match {contract_id} {encoded_field}")
                )
    return issues


def check_adrs(root: Path) -> list[Issue]:
    issues: list[Issue] = []
    adr_dir = root / "docs" / "adr"
    if not adr_dir.is_dir():
        return [issue(root, adr_dir, 1, "ADR directory is missing")]

    records: dict[str, list[tuple[Path, FrontMatter]]] = {}
    paths = sorted(
        path for path in adr_dir.glob("*.md")
        if path.name not in {"README.md", "template.md"}
    )

    for path in paths:
        name_match = ADR_FILE_RE.fullmatch(path.name)
        if not name_match:
            issues.append(issue(root, path, 1, "invalid ADR filename"))
            continue
        front = parse_front_matter(path)
        number = name_match.group(1)
        records.setdefault(number, []).append((path, front))
        for key in ("adr", "title", "status", "date", "deciders"):
            if not front.values.get(key):
                issues.append(issue(root, path, 1, f"missing ADR field {key!r}"))
        if front.values.get("adr") and front.values["adr"] != number:
            issues.append(issue(root, path, front.lines.get("adr", 1), "ADR number does not match filename"))
        status = front.values.get("status", "")
        if status and status not in {"proposed", "accepted", "rejected", "superseded"}:
            issues.append(issue(root, path, front.lines.get("status", 1), f"invalid ADR status {status!r}"))
        date_value = front.values.get("date", "")
        if date_value:
            try:
                date.fromisoformat(date_value)
            except ValueError:
                issues.append(issue(root, path, front.lines.get("date", 1), "ADR date must be YYYY-MM-DD"))
        if status == "superseded" and not front.values.get("superseded-by"):
            issues.append(issue(root, path, 1, "superseded ADR lacks superseded-by"))
        if status != "superseded" and front.values.get("superseded-by"):
            issues.append(
                issue(root, path, front.lines.get("superseded-by", 1),
                      "only a superseded ADR may declare superseded-by")
            )
        if front.values.get("supersedes") and status != "accepted":
            issues.append(
                issue(root, path, front.lines.get("supersedes", 1),
                      "only an accepted ADR may supersede another ADR")
            )

        text = read_text(path)
        for section in ADR_SECTIONS:
            if not re.search(rf"^## {re.escape(section)}\s*$", text, flags=re.MULTILINE):
                issues.append(issue(root, path, 1, f"missing ADR section {section!r}"))

    for number, entries in records.items():
        if len(entries) > 1:
            first = relative_path(root, entries[0][0])
            for path, _ in entries[1:]:
                issues.append(
                    issue(root, path, 1, f"duplicate ADR number {number}; first declared in {first}")
                )

    canonical = {number: entries[0] for number, entries in records.items()}
    for number, (path, front) in canonical.items():
        for field, reciprocal in (
            ("supersedes", "superseded-by"),
            ("superseded-by", "supersedes"),
        ):
            reference = front.values.get(field, "")
            if not reference:
                continue
            line = front.lines.get(field, 1)
            if not ADR_REFERENCE_RE.fullmatch(reference):
                issues.append(issue(root, path, line, f"{field} must be a four-digit ADR number"))
                continue
            if reference == number:
                issues.append(issue(root, path, line, f"ADR must not {field} itself"))
                continue
            if reference not in canonical:
                issues.append(issue(root, path, line, f"{field} references missing ADR {reference}"))
                continue

            target_path, target_front = canonical[reference]
            if target_front.values.get(reciprocal, "") != number:
                target = relative_path(root, target_path)
                issues.append(
                    issue(
                        root,
                        path,
                        line,
                        f"ADR {reference} in {target} must declare {reciprocal}: {number}",
                    )
                )
            if field == "supersedes" and target_front.values.get("status") != "superseded":
                issues.append(
                    issue(root, path, line, f"superseded ADR {reference} must have status superseded")
                )
            if field == "superseded-by" and target_front.values.get("status") != "accepted":
                issues.append(
                    issue(root, path, line, f"superseding ADR {reference} must have status accepted")
                )
    return issues


def check_repository(root: Path = ROOT) -> list[Issue]:
    root = root.resolve()
    paths = contract_paths(root)
    contract_issues, contracts = validate_contract_documents(root, paths)
    issues = list(contract_issues)
    issues.extend(check_links(root, markdown_files(root)))
    issues.extend(check_preview_markers(root, (root / path for path in ENTRY_DOCUMENTS)))
    issues.extend(check_forbidden_claims(root, (root / path for path in CLAIM_SURFACES)))
    issues.extend(check_canonical_versions(root, contracts))
    issues.extend(check_adrs(root))
    return sorted(issues, key=lambda item: (str(item.path), item.line, item.message))


def main() -> int:
    issues = check_repository()
    if issues:
        for found in issues:
            print(f"{found.path.as_posix()}:{found.line}: {found.message}", file=sys.stderr)
        print(f"[FAIL] documentation guard found {len(issues)} issue(s)", file=sys.stderr)
        return 1
    print("[OK] documentation governance guard passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
