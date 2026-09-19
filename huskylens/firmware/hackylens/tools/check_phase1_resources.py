#!/usr/bin/env python3
"""Enforce the pinned Phase 1 firmware resource budget."""

from __future__ import annotations

import argparse
import ast
from collections import Counter
import hashlib
import io
import json
import math
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import build_firmware
from board_contract import ContractError, load_board
from gen_flash_layout import load_validated


DEFAULT_BASELINE = ROOT / "docs" / "evidence" / "phase1-baseline.json"
PINNED_BASELINE_SHA256 = (
    "75d8300766266c144847d1805541ca2303a1fffd1b4496649e50f38af3bb889f"
)
SOURCE_ROOTS = ("firmware", "boards", "platforms")
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}
DIRECT_RUNTIME_CALLS = {
    "malloc", "calloc", "realloc", "aligned_alloc", "pvportmalloc",
    "strdup", "_strdup",
    "xtaskcreate", "xtaskcreatestatic", "xqueuecreate", "xqueuecreatestatic",
    "pthread_create", "thrd_create", "std::thread",
    "make_unique", "make_shared",
}
WRAPPER_RUNTIME_MARKERS = ("alloc", "task", "queue", "spawn", "thread", "worker")
CALL_RE = re.compile(
    r"(?<![A-Za-z0-9_])(?P<name>(?:::)?[A-Za-z_][A-Za-z0-9_]*"
    r"(?:::[A-Za-z_][A-Za-z0-9_]*)*)\s*"
    r"(?:<[^;{}()]*>)?\s*\("
)
CXX_NEW_RE = re.compile(
    r"(?<![A-Za-z0-9_])(?:new(?:\s*\([^()]*\))?\s+"
    r"(?:const\s+|volatile\s+)*[A-Za-z_:]|::\s*operator\s+new\s*\()",
    re.MULTILINE,
)
STD_THREAD_OBJECT_RE = re.compile(
    r"(?<![A-Za-z0-9_])std\s*::\s*thread\s+"
    r"[A-Za-z_][A-Za-z0-9_]*\s*(?:\(|\{)",
    re.MULTILINE,
)
ALIAS_RE = re.compile(
    r"(?<![A-Za-z0-9_])(?P<alias>[A-Za-z_][A-Za-z0-9_]*)\s*"
    r"(?:\)\s*\([^;=]*\))?\s*=\s*&?\s*"
    r"(?P<target>(?:::)?[A-Za-z_][A-Za-z0-9_]*"
    r"(?:::[A-Za-z_][A-Za-z0-9_]*)*)\b(?!\s*(?:<[^;{}()]*>)?\s*\()"
)
MACRO_RE = re.compile(
    r"(?m)^\s*#\s*define\s+(?P<name>[A-Za-z_][A-Za-z0-9_]*)"
    r"(?:\s*\([^\r\n]*?\))?\s+(?P<body>[^\r\n]+)$"
)
FUNCTION_RE = re.compile(
    r"(?P<name>(?:::)?[A-Za-z_][A-Za-z0-9_]*"
    r"(?:::[A-Za-z_~][A-Za-z0-9_]*)*)\s*"
    r"\([^;{}]*\)\s*(?:const\s*)?(?:noexcept\s*)?\{",
    re.MULTILINE,
)
CONTROL_NAMES = {
    "if", "for", "while", "switch", "catch", "sizeof", "alignof",
    "decltype", "static_assert", "return", "defined",
}
GIT_SHA_RE = re.compile(r"^[0-9a-f]{40}$")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
BASELINE_ROOT_FIELDS = {"schema", "baseline", "acceptance", "formulas", "toolchain"}
BASELINE_FIELDS = {
    "commit", "firmware_version", "implicit_board", "profile",
    "text_bytes", "data_bytes", "bss_bytes", "static_ram_bytes", "raw_image_bytes",
    "flash_occupied_bytes", "manually_pinned_golden_bin_sha256",
    "manually_pinned_golden_elf_sha256",
}
ACCEPTANCE_FIELDS = {
    "flash_delta_max_bytes", "static_ram_delta_max_bytes",
    "new_background_tasks_queues_or_heap_allocations",
}
FORMULA_FIELDS = {"flash_occupied", "static_ram"}
TOOLCHAIN_FIELDS = {
    "archive_sha256", "operator_recorded_cmake", "kendryte_standalone_sdk_revision",
    "kendryte_toolchain",
}
RESULT_FIELDS = {
    "schema", "board", "firmware_version", "baseline_commit", "image", "elf",
    "hash_scope", "new_heap_allocations_background_tasks_or_queues", "accepted",
}
RESULT_IMAGE_FIELDS = {
    "path", "local_sha256", "raw_bytes", "flash_occupied_bytes", "flash_delta_bytes",
}
RESULT_ELF_FIELDS = {
    "path", "local_sha256", "text_bytes", "data_bytes", "bss_bytes",
    "static_ram_bytes", "static_ram_delta_bytes",
}
LOCAL_HASH_SCOPE = "local-workspace-diagnostic"
TRIGRAPHS = {
    "??=": "#", "??/": "\\", "??'": "^", "??(": "[", "??)": "]",
    "??!": "|", "??<": "{", "??>": "}", "??-": "~",
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def find_size() -> Path:
    toolchain = build_firmware.find_toolchain_bin()
    if toolchain:
        for name in ("riscv64-unknown-elf-size.exe", "riscv64-unknown-elf-size"):
            candidate = toolchain / name
            if candidate.is_file():
                return candidate
    for name in ("riscv64-unknown-elf-size", "riscv64-unknown-elf-size.exe"):
        candidate = shutil.which(name)
        if candidate:
            return Path(candidate)
    raise RuntimeError("riscv64-unknown-elf-size not found")


def parse_size_output(output: str) -> tuple[int, int, int]:
    lines = [line.split() for line in output.splitlines() if line.strip()]
    if len(lines) < 2 or lines[0][:3] != ["text", "data", "bss"]:
        raise ValueError("unexpected size output")
    try:
        text_bytes, data_bytes, bss_bytes = (int(value) for value in lines[1][:3])
    except (ValueError, IndexError) as exc:
        raise ValueError("unexpected size values") from exc
    return text_bytes, data_bytes, bss_bytes


def read_elf_size(elf: Path) -> tuple[int, int, int]:
    result = subprocess.run(
        [str(find_size()), str(elf)],
        check=True,
        text=True,
        capture_output=True,
    )
    return parse_size_output(result.stdout)


def _splice_translation_phases(source: str) -> str:
    """Apply C trigraph replacement and backslash-newline splicing."""

    source = source.replace("\r\n", "\n").replace("\r", "\n")
    result: list[str] = []
    index = 0
    while index < len(source):
        trigraph = TRIGRAPHS.get(source[index:index + 3])
        current = trigraph if trigraph is not None else source[index]
        consumed = 3 if trigraph is not None else 1
        if current == "\\":
            newline = index + consumed
            while newline < len(source) and source[newline] in " \t\f\v":
                newline += 1
            if newline < len(source) and source[newline] == "\n":
                index = newline + 1
                continue
        result.append(current)
        index += consumed
    return "".join(result)


def _strip_c_comments_and_literals(source: str) -> str:
    """Blank comments and literals while preserving offsets and line numbers."""

    source = _splice_translation_phases(source)
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
            if current == "\\" and following:
                result.extend((" ", "\n" if following == "\n" else " "))
                index += 2
                continue
            terminator = '"' if state == "string" else "'"
            result.append("\n" if current == "\n" else " ")
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
            if current == '"':
                state = "string"
                result.append(" ")
            elif current == "'":
                state = "character"
                result.append(" ")
            else:
                result.append(current)
            index += 1
    return "".join(result)


def _matching_brace(code: str, opening: int) -> int:
    depth = 0
    for index in range(opening, len(code)):
        if code[index] == "{":
            depth += 1
        elif code[index] == "}":
            depth -= 1
            if depth == 0:
                return index
    return len(code) - 1


def _expression_fingerprint(code: str, start: int, *, limit: int = 240) -> str:
    """Return a normalized statement-sized fingerprint around one site."""

    end = start
    parens = 0
    while end < len(code) and end - start < limit:
        character = code[end]
        if character == "(":
            parens += 1
        elif character == ")" and parens:
            parens -= 1
        if character in ";\n" and parens == 0:
            break
        end += 1
    return re.sub(r"\s+", "", code[start:end])


def _site_context_fingerprint(code: str, start: int) -> str:
    """Fingerprint brace ancestry and the site's statement-line prefix."""

    stack: list[str] = []
    delimiter = 0
    for index, character in enumerate(code[:start]):
        if character == "{":
            header = re.sub(r"\s+", "", code[delimiter:index + 1])
            stack.append(header)
            delimiter = index + 1
        elif character == "}":
            if stack:
                stack.pop()
            delimiter = index + 1
        elif character == ";":
            delimiter = index + 1
    line_start = code.rfind("\n", 0, start) + 1
    line_prefix = re.sub(r"\s+", "", code[line_start:start])
    context = "|".join([*stack, f"line:{line_prefix}"])
    return hashlib.sha256(context.encode("utf-8")).hexdigest()


def _function_regions(code: str) -> list[tuple[str, int, int, int]]:
    regions: list[tuple[str, int, int, int]] = []
    occupied_until = -1
    for match in FUNCTION_RE.finditer(code):
        name = match.group("name")
        if name.rsplit("::", 1)[-1] in CONTROL_NAMES or match.start() < occupied_until:
            continue
        opening = code.find("{", match.start(), match.end())
        closing = _matching_brace(code, opening)
        regions.append((name, match.start(), opening + 1, closing))
        occupied_until = closing
    return regions


def _runtime_sites(snapshot: dict[str, str]) -> list[dict[str, str | int]]:
    """Conservatively find creation sites and their tainted wrappers.

    This is a lexical policy guard, not a full C/C++ semantic analyser.  It
    removes comments/literals, scans function bodies (not prototypes), follows
    explicit function aliases and wrapper calls, and fingerprints each site so
    a delete/add move cannot be hidden by an unchanged repository-wide count.
    """

    files: dict[str, tuple[str, str, list[tuple[str, int, int, int]]]] = {}
    functions: list[dict[str, object]] = []
    primitive_names = {
        name.lstrip(":").rsplit("::", 1)[-1].lower()
        for name in DIRECT_RUNTIME_CALLS
    }
    for relative, source in sorted(snapshot.items()):
        code = _strip_c_comments_and_literals(source)
        normalized_hash = hashlib.sha256(
            re.sub(r"\s+", "", code).encode("utf-8")
        ).hexdigest()
        regions = _function_regions(code)
        files[relative] = (code, normalized_hash, regions)
        for name, definition, body_start, body_end in regions:
            functions.append({
                "path": relative,
                "name": name,
                "definition": definition,
                "body_start": body_start,
                "body_end": body_end,
                "body": code[body_start:body_end],
            })

    aliases_by_file: dict[str, set[str]] = {}
    for relative, (code, _digest, _regions) in files.items():
        file_aliases: set[str] = set()
        for match in ALIAS_RE.finditer(code):
            target = match.group("target").lstrip(":").rsplit("::", 1)[-1].lower()
            if target in primitive_names:
                file_aliases.add(match.group("alias"))
        for match in MACRO_RE.finditer(code):
            body_names = {
                token.lstrip(":").rsplit("::", 1)[-1].lower()
                for token in re.findall(
                    r"(?:::)?[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*",
                    match.group("body"),
                )
            }
            if body_names & primitive_names:
                file_aliases.add(match.group("name"))
        aliases_by_file[relative] = file_aliases
    # Headers and external function-pointer declarations can expose an alias to
    # another translation unit.  Conservatively treat explicit alias names as
    # repository-wide for use-site detection; definitions remain file-scoped
    # evidence sites below.
    global_aliases = {
        alias.lower()
        for aliases in aliases_by_file.values()
        for alias in aliases
    }

    tainted: set[str] = set()
    changed = True
    while changed:
        changed = False
        for function in functions:
            name = str(function["name"])
            body = str(function["body"])
            path = str(function["path"])
            known = primitive_names | {
                wrapper.lstrip(":").rsplit("::", 1)[-1].lower()
                for wrapper in tainted
            } | {
                alias.lower() for alias in aliases_by_file.get(path, set())
            }
            calls = {
                match.group("name").lstrip(":").rsplit("::", 1)[-1].lower()
                for match in CALL_RE.finditer(body)
            }
            if (
                calls & known
                or CXX_NEW_RE.search(body)
                or STD_THREAD_OBJECT_RE.search(body)
            ):
                if name not in tainted:
                    tainted.add(name)
                    changed = True

    tainted_base = {name.lstrip(":").rsplit("::", 1)[-1].lower() for name in tainted}
    sites: list[dict[str, str | int]] = []

    def add_site(
        signature: str, relative: str, code: str, position: int,
        function: str, fingerprint: str,
    ) -> None:
        sites.append({
            "signature": signature,
            "path": relative,
            "line": code.count("\n", 0, position) + 1,
            "function": function,
            "fingerprint": fingerprint,
            "context": _site_context_fingerprint(code, position),
            "file_hash": files[relative][1],
        })

    for relative, (code, _digest, regions) in files.items():
        file_aliases = global_aliases
        for match in ALIAS_RE.finditer(code):
            target = match.group("target").lstrip(":").rsplit("::", 1)[-1].lower()
            if target in primitive_names or target in tainted_base:
                add_site(
                    f"alias:{match.group('alias')}", relative, code, match.start(),
                    "<file>", _expression_fingerprint(code, match.start()),
                )
        for match in MACRO_RE.finditer(code):
            body_names = {
                token.lstrip(":").rsplit("::", 1)[-1].lower()
                for token in re.findall(
                    r"(?:::)?[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*",
                    match.group("body"),
                )
            }
            if body_names & primitive_names:
                add_site(
                    f"alias:{match.group('name')}", relative, code, match.start(),
                    "<macro>", re.sub(r"\s+", "", match.group(0)),
                )
        for name, definition, body_start, body_end in regions:
            body = code[body_start:body_end]
            for match in CALL_RE.finditer(body):
                raw_name = match.group("name")
                basename = raw_name.lstrip(":").rsplit("::", 1)[-1].lower()
                if (
                    basename not in primitive_names
                    and basename not in tainted_base
                    and basename not in file_aliases
                    and not any(marker in basename for marker in WRAPPER_RUNTIME_MARKERS)
                ):
                    continue
                absolute = body_start + match.start()
                add_site(
                    f"call:{raw_name}", relative, code, absolute, name,
                    _expression_fingerprint(code, absolute),
                )
            for match in CXX_NEW_RE.finditer(body):
                absolute = body_start + match.start()
                add_site(
                    "cxx:new", relative, code, absolute, name,
                    _expression_fingerprint(code, absolute),
                )
            for match in STD_THREAD_OBJECT_RE.finditer(body):
                absolute = body_start + match.start()
                add_site(
                    "call:std::thread", relative, code, absolute, name,
                    _expression_fingerprint(code, absolute),
                )
        for matcher, signature in (
            (CXX_NEW_RE, "cxx:new"),
            (STD_THREAD_OBJECT_RE, "call:std::thread"),
        ):
            for match in matcher.finditer(code):
                if any(body_start <= match.start() < body_end
                       for _name, _definition, body_start, body_end in regions):
                    continue
                add_site(
                    signature, relative, code, match.start(), "<file>",
                    _expression_fingerprint(code, match.start()),
                )
        for match in CALL_RE.finditer(code):
            if any(body_start <= match.start() < body_end
                   for _name, _definition, body_start, body_end in regions):
                continue
            raw_name = match.group("name")
            basename = raw_name.lstrip(":").rsplit("::", 1)[-1].lower()
            if basename in CONTROL_NAMES:
                continue
            if (
                basename not in primitive_names
                and basename not in tainted_base
                and basename not in file_aliases
                and not any(marker in basename
                            for marker in WRAPPER_RUNTIME_MARKERS)
            ):
                continue
            add_site(
                f"call:{raw_name}", relative, code, match.start(), "<file>",
                _expression_fingerprint(code, match.start()),
            )
    return sites


def _runtime_occurrences(
    snapshot: dict[str, str],
) -> dict[str, list[str]]:
    occurrences: dict[str, list[str]] = {}
    for site in _runtime_sites(snapshot):
        signature = str(site["signature"])
        occurrences.setdefault(signature, []).append(
            f"{site['path']}:{site['line']}"
        )
    return occurrences


def _baseline_source_snapshot(commit: str, *, root: Path) -> dict[str, str]:
    top_level = subprocess.run(
        ["git", "ls-tree", "-d", "--name-only", commit],
        cwd=root,
        check=True,
        text=True,
        capture_output=True,
    ).stdout.splitlines()
    scopes = [scope for scope in SOURCE_ROOTS if scope in top_level]
    if not scopes:
        return {}
    archive = subprocess.run(
        ["git", "archive", "--format=tar", commit, "--", *scopes],
        cwd=root,
        check=True,
        capture_output=True,
    ).stdout
    snapshot: dict[str, str] = {}
    with tarfile.open(fileobj=io.BytesIO(archive), mode="r:") as bundle:
        for member in bundle.getmembers():
            if not member.isfile() or Path(member.name).suffix.lower() not in SOURCE_SUFFIXES:
                continue
            source = bundle.extractfile(member)
            if source is not None:
                snapshot[Path(member.name).as_posix()] = source.read().decode(
                    "utf-8", errors="replace"
                )
    return snapshot


def _current_source_snapshot(*, root: Path) -> dict[str, str]:
    snapshot: dict[str, str] = {}
    for scope in SOURCE_ROOTS:
        directory = root / scope
        if not directory.is_dir():
            continue
        for path in directory.rglob("*"):
            if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
                continue
            relative = path.relative_to(root).as_posix()
            snapshot[relative] = path.read_text(encoding="utf-8", errors="replace")
    return snapshot


def added_runtime_objects(commit: str, *, root: Path = ROOT) -> list[str]:
    """Report current creation sites not grandfathered by a baseline site."""

    baseline = _runtime_sites(_baseline_source_snapshot(commit, root=root))
    current = _runtime_sites(_current_source_snapshot(root=root))
    baseline_file_paths: dict[str, set[str]] = {}
    for site in baseline:
        baseline_file_paths.setdefault(str(site["file_hash"]), set()).add(
            str(site["path"])
        )

    def keys(
        site: dict[str, str | int]
    ) -> set[tuple[str, str, str, str, str, int]]:
        signature = str(site["signature"])
        function = str(site["function"])
        fingerprint = str(site["fingerprint"])
        context = str(site["context"])
        line = int(site["line"])
        paths = {str(site["path"])}
        # Exact normalized file content may be renamed without manufacturing a
        # new runtime object. Modified/copy-pasted files do not get this waiver.
        paths.update(baseline_file_paths.get(str(site["file_hash"]), set()))
        return {
            (signature, path, function, fingerprint, context, line)
            for path in paths
        }

    baseline_keys = Counter(
        (
            str(site["signature"]), str(site["path"]),
            str(site["function"]), str(site["fingerprint"]),
            str(site["context"]), int(site["line"]),
        )
        for site in baseline
    )
    findings: list[str] = []
    for site in current:
        matches = [key for key in keys(site) if baseline_keys[key] > 0]
        if matches:
            baseline_keys[matches[0]] -= 1
            continue
        findings.append(
            f"{site['signature']} introduced at {site['path']}:{site['line']} "
            f"in {site['function']}"
        )
    return sorted(findings)


def _exact_fields(value: Any, expected: set[str], field: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise RuntimeError(f"resource baseline {field}: expected object")
    unknown = sorted(set(value) - expected)
    missing = sorted(expected - set(value))
    if unknown or missing:
        details = []
        if unknown:
            details.append("unknown=" + ",".join(unknown))
        if missing:
            details.append("missing=" + ",".join(missing))
        raise RuntimeError(f"resource baseline {field}: " + "; ".join(details))
    return value


def _integer(value: Any, field: str, *, minimum: int = 0) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        raise RuntimeError(f"resource baseline {field}: expected integer >= {minimum}")
    return value


def _string(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value:
        raise RuntimeError(f"resource baseline {field}: expected non-empty string")
    return value


def canonical_json_bytes(document: Any) -> bytes:
    return (
        json.dumps(
            document,
            ensure_ascii=False,
            allow_nan=False,
            sort_keys=True,
            indent=2,
            separators=(",", ": "),
        ).encode("utf-8") + b"\n"
    )


def validate_baseline_document(document: Any) -> dict[str, object]:
    document = _exact_fields(document, BASELINE_ROOT_FIELDS, "root")
    if type(document["schema"]) is not int or document["schema"] != 1:
        raise RuntimeError("resource baseline schema: expected integer 1")
    baseline = _exact_fields(document["baseline"], BASELINE_FIELDS, "baseline")
    acceptance = _exact_fields(
        document["acceptance"], ACCEPTANCE_FIELDS, "acceptance"
    )
    formulas = _exact_fields(document["formulas"], FORMULA_FIELDS, "formulas")
    toolchain = _exact_fields(document["toolchain"], TOOLCHAIN_FIELDS, "toolchain")
    for field in (
        "text_bytes", "data_bytes", "bss_bytes", "static_ram_bytes",
        "raw_image_bytes", "flash_occupied_bytes",
    ):
        _integer(baseline[field], f"baseline.{field}")
    if baseline["static_ram_bytes"] != baseline["data_bytes"] + baseline["bss_bytes"]:
        raise RuntimeError("resource baseline static RAM does not equal data + bss")
    for field in ("firmware_version", "implicit_board", "profile"):
        _string(baseline[field], f"baseline.{field}")
    if GIT_SHA_RE.fullmatch(_string(baseline["commit"], "baseline.commit")) is None:
        raise RuntimeError("resource baseline commit: expected lowercase Git SHA-1")
    for field in (
        "manually_pinned_golden_bin_sha256",
        "manually_pinned_golden_elf_sha256",
    ):
        if SHA256_RE.fullmatch(_string(baseline[field], f"baseline.{field}")) is None:
            raise RuntimeError(f"resource baseline {field}: expected lowercase SHA-256")
    _integer(acceptance["flash_delta_max_bytes"], "acceptance.flash_delta_max_bytes")
    _integer(
        acceptance["new_background_tasks_queues_or_heap_allocations"],
        "acceptance.new_background_tasks_queues_or_heap_allocations",
    )
    _integer(
        acceptance["static_ram_delta_max_bytes"],
        "acceptance.static_ram_delta_max_bytes",
        minimum=-0x7FFFFFFF,
    )
    if acceptance["flash_delta_max_bytes"] != 8192:
        raise RuntimeError("resource baseline flash delta limit must be 8192")
    if acceptance["static_ram_delta_max_bytes"] != 0:
        raise RuntimeError("resource baseline static RAM growth limit must be zero")
    if acceptance["new_background_tasks_queues_or_heap_allocations"] != 0:
        raise RuntimeError("resource baseline runtime-object allowance must be zero")
    expected_formulas = {
        "flash_occupied": "ceil((raw_image_bytes + 37) / erase_size) * erase_size",
        "static_ram": "data_bytes + bss_bytes",
    }
    if formulas != expected_formulas:
        raise RuntimeError("resource baseline formulas do not match Phase 1 definitions")
    for field in (
        "operator_recorded_cmake", "kendryte_standalone_sdk_revision",
        "kendryte_toolchain",
    ):
        _string(toolchain[field], f"toolchain.{field}")
    if GIT_SHA_RE.fullmatch(toolchain["kendryte_standalone_sdk_revision"]) is None:
        raise RuntimeError("resource baseline SDK revision must be a Git SHA-1")
    if SHA256_RE.fullmatch(
        _string(toolchain["archive_sha256"], "toolchain.archive_sha256")
    ) is None:
        raise RuntimeError("resource baseline toolchain archive hash is invalid")
    return document


def load_baseline(path: Path) -> dict[str, object]:
    try:
        encoded = path.read_bytes()
        document = json.loads(encoded.decode("utf-8"))
        canonical = canonical_json_bytes(document)
    except (OSError, UnicodeDecodeError, json.JSONDecodeError, ValueError) as exc:
        raise RuntimeError(f"cannot read resource baseline {path}: {exc}") from exc
    if encoded != canonical:
        raise RuntimeError("resource baseline bytes are not exact canonical JSON")
    digest = hashlib.sha256(encoded).hexdigest()
    if digest != PINNED_BASELINE_SHA256:
        raise RuntimeError(
            "resource baseline provenance digest mismatch: "
            f"expected {PINNED_BASELINE_SHA256}, got {digest}"
        )
    return validate_baseline_document(document)


def ensure_commit_available(commit: str, *, root: Path = ROOT) -> None:
    result = subprocess.run(
        ["git", "cat-file", "-e", f"{commit}^{{commit}}"],
        cwd=root,
        text=True,
        capture_output=True,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"resource baseline commit {commit} is unavailable; fetch full history"
        )


def ensure_commit_is_ancestor(commit: str, *, root: Path = ROOT) -> None:
    result = subprocess.run(
        ["git", "merge-base", "--is-ancestor", commit, "HEAD"],
        cwd=root,
        text=True,
        capture_output=True,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"resource baseline commit {commit} is not an ancestor of HEAD"
        )


def _git_file(commit: str, relative: str, *, root: Path) -> str:
    result = subprocess.run(
        ["git", "show", f"{commit}:{relative}"],
        cwd=root,
        check=True,
        text=True,
        capture_output=True,
    )
    return result.stdout


def _python_string_constant(source: str, name: str, *, label: str) -> str:
    try:
        tree = ast.parse(source, filename=label)
    except SyntaxError as exc:
        raise RuntimeError(f"cannot parse {label}: {exc}") from exc
    for statement in tree.body:
        if not isinstance(statement, (ast.Assign, ast.AnnAssign)):
            continue
        targets = statement.targets if isinstance(statement, ast.Assign) else [statement.target]
        if not any(isinstance(target, ast.Name) and target.id == name for target in targets):
            continue
        try:
            value = ast.literal_eval(statement.value)
        except (ValueError, TypeError) as exc:
            raise RuntimeError(f"{label} {name} is not a literal string") from exc
        if not isinstance(value, str) or not value:
            raise RuntimeError(f"{label} {name} is not a literal string")
        return value
    raise RuntimeError(f"{label} does not define {name}")


def verify_bootstrap_pins(source: str, toolchain: dict[str, Any], *, label: str) -> None:
    expected_sdk = str(toolchain["kendryte_standalone_sdk_revision"])
    expected_sha = str(toolchain["archive_sha256"])
    expected_version = str(toolchain["kendryte_toolchain"])
    sdk = _python_string_constant(source, "SDK_REVISION", label=label)
    archive_sha = _python_string_constant(source, "TOOLCHAIN_SHA256", label=label)
    archive_url = _python_string_constant(source, "TOOLCHAIN_URL", label=label)
    if sdk != expected_sdk:
        raise RuntimeError(f"{label} SDK revision does not match resource evidence")
    if archive_sha != expected_sha:
        raise RuntimeError(f"{label} toolchain archive SHA-256 does not match evidence")
    if f"/{expected_version}/" not in archive_url:
        raise RuntimeError(f"{label} toolchain version does not match resource evidence")


def verify_baseline_repository_provenance(
    document: dict[str, object], *, root: Path = ROOT
) -> None:
    baseline = document["baseline"]
    toolchain = document["toolchain"]
    assert isinstance(baseline, dict)
    assert isinstance(toolchain, dict)
    commit = str(baseline["commit"])
    ensure_commit_available(commit, root=root)
    ensure_commit_is_ancestor(commit, root=root)
    historical_version = _git_file(commit, "VERSION", root=root).strip()
    if historical_version != baseline["firmware_version"]:
        raise RuntimeError(
            "resource baseline firmware version does not match baseline commit VERSION"
        )
    historical_bootstrap = _git_file(commit, "tools/bootstrap_deps.py", root=root)
    verify_bootstrap_pins(
        historical_bootstrap, toolchain, label=f"baseline {commit} bootstrap"
    )
    try:
        current_bootstrap = (root / "tools" / "bootstrap_deps.py").read_text(
            encoding="utf-8"
        )
    except OSError as exc:
        raise RuntimeError(f"cannot read current bootstrap: {exc}") from exc
    verify_bootstrap_pins(current_bootstrap, toolchain, label="current bootstrap")


def ensure_tracked_result(path: Path, *, root: Path = ROOT) -> None:
    try:
        relative = path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError as exc:
        raise RuntimeError("resource result must be a tracked repository file") from exc
    result = subprocess.run(
        ["git", "ls-files", "--error-unmatch", "--", relative],
        cwd=root,
        text=True,
        capture_output=True,
    )
    if result.returncode != 0:
        raise RuntimeError(f"resource result is not tracked: {relative}")


def validate_result_document(document: Any) -> dict[str, Any]:
    if not isinstance(document, dict) or set(document) != RESULT_FIELDS:
        raise RuntimeError("tracked result root has missing or unknown fields")
    if type(document["schema"]) is not int or document["schema"] != 1:
        raise RuntimeError("tracked result schema must be integer 1")
    for field in ("board", "firmware_version", "baseline_commit"):
        if not isinstance(document[field], str) or not document[field]:
            raise RuntimeError(f"tracked result {field} must be a non-empty string")
    if GIT_SHA_RE.fullmatch(document["baseline_commit"]) is None:
        raise RuntimeError("tracked result baseline_commit must be a Git SHA-1")
    if document["hash_scope"] != LOCAL_HASH_SCOPE:
        raise RuntimeError(
            f"tracked result hash_scope must be {LOCAL_HASH_SCOPE!r}"
        )
    image = document["image"]
    elf = document["elf"]
    if not isinstance(image, dict) or set(image) != RESULT_IMAGE_FIELDS:
        raise RuntimeError("tracked result image has missing or unknown fields")
    if not isinstance(elf, dict) or set(elf) != RESULT_ELF_FIELDS:
        raise RuntimeError("tracked result elf has missing or unknown fields")
    for group_name, group, fields in (
        ("image", image, RESULT_IMAGE_FIELDS),
        ("elf", elf, RESULT_ELF_FIELDS),
    ):
        if not isinstance(group["path"], str) or not group["path"]:
            raise RuntimeError(f"tracked result {group_name}.path is invalid")
        if (not isinstance(group["local_sha256"], str) or
                SHA256_RE.fullmatch(group["local_sha256"]) is None):
            raise RuntimeError(f"tracked result {group_name}.local_sha256 is invalid")
        for field in fields - {"path", "local_sha256"}:
            if isinstance(group[field], bool) or not isinstance(group[field], int):
                raise RuntimeError(f"tracked result {group_name}.{field} must be an integer")
    findings = document["new_heap_allocations_background_tasks_or_queues"]
    if not isinstance(findings, list) or any(not isinstance(item, str) for item in findings):
        raise RuntimeError("tracked result allocation findings must be a string array")
    if not isinstance(document["accepted"], bool):
        raise RuntimeError("tracked result accepted must be boolean")
    return document


def deterministic_result_projection(document: dict[str, Any]) -> dict[str, Any]:
    """Omit local diagnostic hashes while retaining every budget metric."""
    return {
        "schema": document["schema"],
        "board": document["board"],
        "firmware_version": document["firmware_version"],
        "baseline_commit": document["baseline_commit"],
        "hash_scope": document["hash_scope"],
        "image": {
            field: document["image"][field]
            for field in ("path", "raw_bytes", "flash_occupied_bytes", "flash_delta_bytes")
        },
        "elf": {
            field: document["elf"][field]
            for field in (
                "path", "text_bytes", "data_bytes", "bss_bytes",
                "static_ram_bytes", "static_ram_delta_bytes",
            )
        },
        "new_heap_allocations_background_tasks_or_queues":
            document["new_heap_allocations_background_tasks_or_queues"],
        "accepted": document["accepted"],
    }


def resource_budget_passes(
    flash_delta: int,
    static_delta: int,
    findings: list[str],
    acceptance: dict[str, object],
) -> bool:
    return (
        flash_delta <= int(acceptance["flash_delta_max_bytes"])
        and static_delta <= int(acceptance["static_ram_delta_max_bytes"])
        and not findings
    )


def measure(board_id: str, baseline_path: Path, image: Path,
            elf: Path) -> dict[str, object]:
    image = image.resolve()
    elf = elf.resolve()
    baseline_document = load_baseline(baseline_path)
    baseline = baseline_document["baseline"]
    acceptance = baseline_document["acceptance"]
    assert isinstance(baseline, dict)
    assert isinstance(acceptance, dict)
    verify_baseline_repository_provenance(baseline_document)
    if baseline.get("implicit_board") != board_id:
        raise RuntimeError(
            f"baseline applies to {baseline.get('implicit_board')!r}, not {board_id!r}"
        )
    if not image.is_file() or not elf.is_file():
        raise RuntimeError("full firmware image and ELF must exist before resource check")

    board = load_board(board_id)
    flash, _partitions = load_validated(board.flash_layout_path)
    baseline_flash_occupied = math.ceil(
        (int(baseline["raw_image_bytes"]) + build_firmware.K210_IMAGE_OVERHEAD)
        / flash["erase_size"]
    ) * flash["erase_size"]
    if baseline_flash_occupied != int(baseline["flash_occupied_bytes"]):
        raise RuntimeError(
            "resource baseline flash occupancy does not match the canonical formula"
        )
    raw_image = image.stat().st_size
    flash_occupied = math.ceil(
        (raw_image + build_firmware.K210_IMAGE_OVERHEAD) / flash["erase_size"]
    ) * flash["erase_size"]
    text_bytes, data_bytes, bss_bytes = read_elf_size(elf)
    static_ram = data_bytes + bss_bytes
    flash_delta = flash_occupied - int(baseline["flash_occupied_bytes"])
    static_delta = static_ram - int(baseline["static_ram_bytes"])
    findings = added_runtime_objects(str(baseline["commit"]))

    result: dict[str, object] = {
        "schema": 1,
        "board": board.id,
        "firmware_version": (ROOT / "VERSION").read_text(encoding="utf-8").strip(),
        "baseline_commit": baseline["commit"],
        "hash_scope": LOCAL_HASH_SCOPE,
        "image": {
            "path": image.relative_to(ROOT).as_posix(),
            "local_sha256": sha256(image),
            "raw_bytes": raw_image,
            "flash_occupied_bytes": flash_occupied,
            "flash_delta_bytes": flash_delta,
        },
        "elf": {
            "path": elf.relative_to(ROOT).as_posix(),
            "local_sha256": sha256(elf),
            "text_bytes": text_bytes,
            "data_bytes": data_bytes,
            "bss_bytes": bss_bytes,
            "static_ram_bytes": static_ram,
            "static_ram_delta_bytes": static_delta,
        },
        "new_heap_allocations_background_tasks_or_queues": findings,
        "accepted": resource_budget_passes(
            flash_delta, static_delta, findings, acceptance
        ),
    }
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board", required=True)
    parser.add_argument("--baseline", type=Path, default=DEFAULT_BASELINE)
    parser.add_argument("--image", type=Path)
    parser.add_argument("--elf", type=Path)
    parser.add_argument("--write-result", type=Path)
    parser.add_argument("--check-result", type=Path)
    args = parser.parse_args(argv)
    if args.write_result and args.check_result:
        parser.error("--write-result and --check-result are mutually exclusive")
    image = args.image or ROOT / "build" / args.board / "hackylens-full.bin"
    elf = args.elf or ROOT / "build" / args.board / "sdk-full" / "hackylens_full"
    try:
        result = measure(args.board, args.baseline, image, elf)
    except (ContractError, OSError, RuntimeError, ValueError,
            subprocess.CalledProcessError) as exc:
        print(f"[ERR] resource check failed: {exc}", file=sys.stderr)
        return 2
    encoded = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.write_result:
        args.write_result.parent.mkdir(parents=True, exist_ok=True)
        args.write_result.write_text(encoded, encoding="utf-8", newline="\n")
    if args.check_result:
        try:
            ensure_tracked_result(args.check_result)
            tracked = validate_result_document(
                json.loads(args.check_result.read_text(encoding="utf-8"))
            )
        except (OSError, json.JSONDecodeError, RuntimeError) as exc:
            print(f"[ERR] tracked resource result is invalid: {exc}", file=sys.stderr)
            return 2
        current_projection = deterministic_result_projection(result)
        tracked_projection = deterministic_result_projection(tracked)
        if current_projection != tracked_projection:
            print("[ERR] tracked resource result is stale", file=sys.stderr)
            print(json.dumps({"tracked": tracked_projection,
                              "current": current_projection}, indent=2, sort_keys=True),
                  file=sys.stderr)
            return 1
    print(encoded, end="")
    if not result["accepted"]:
        print("[ERR] Phase 1 resource acceptance failed", file=sys.stderr)
        return 1
    print("[OK] Phase 1 resource acceptance passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
