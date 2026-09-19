#!/usr/bin/env python3
"""Validate every known board descriptor, BSP and generated output."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from board_contract import Board, ContractError, ROOT, board_ids, load_board
from gen_board import generate


def compile_conformance_board(board: Board, *, compiler: str | None = None) -> None:
    """Compile and link the selected BSP against every generated header."""
    if board.support != "conformance":
        raise ContractError("conformance compile requires support=conformance")
    compiler = compiler or os.environ.get("CC") or shutil.which("gcc") or shutil.which("clang")
    if not compiler:
        raise ContractError("host C compiler unavailable for conformance compile")
    with tempfile.TemporaryDirectory(prefix=f"hk-{board.id}-") as temporary:
        output = Path(temporary) / "board-conformance"
        command = [
            compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            f"-I{ROOT / 'firmware' / 'src' / 'internal'}",
            f"-I{board.generated_dir}",
            str(board.directory / "board.c"),
            str(ROOT / "tools" / "board_conformance_harness.c"),
            "-o",
            str(output),
        ]
        print("+ " + " ".join(command))
        subprocess.run(command, check=True)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    selection = parser.add_mutually_exclusive_group(required=True)
    selection.add_argument("--board", help="Canonical board.toml ID")
    selection.add_argument("--all", action="store_true", help="Check every known board")
    parser.add_argument("--generate", action="store_true",
                        help="write generated outputs instead of stale-checking")
    parser.add_argument("--compile", action="store_true",
                        help="Host compile-check a selected conformance BSP")
    args = parser.parse_args(argv)
    failures: list[str] = []
    try:
        identifiers = board_ids() if args.all else [args.board]
        if not identifiers:
            raise ContractError("no board descriptors found")
        for identifier in identifiers:
            board = load_board(identifier)
            failures.extend(generate(board, check=not args.generate))
            if args.compile:
                if args.all:
                    raise ContractError("--compile requires one explicit --board")
                if board.support != "conformance":
                    raise ContractError("--compile is reserved for support=conformance BSPs")
                compile_conformance_board(board)
    except (ContractError, ValueError, subprocess.CalledProcessError) as exc:
        print(f"board contract error: {exc}", file=sys.stderr)
        return 2
    if failures:
        for failure in failures:
            print(failure, file=sys.stderr)
        return 1
    print(f"[OK] Board Port Contract passed for {len(identifiers)} board(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
