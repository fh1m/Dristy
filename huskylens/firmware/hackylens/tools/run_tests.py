#!/usr/bin/env python3
"""Run the HackyLens unittest suite and reject skipped coverage."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]


def result_exit_code(result: unittest.TestResult) -> int:
    if result.skipped:
        print(
            f"[FAIL] strict test run skipped {len(result.skipped)} test(s):",
            file=sys.stderr,
        )
        for test, reason in result.skipped:
            print(f"  {test}: {reason}", file=sys.stderr)
        return 1
    return 0 if result.wasSuccessful() else 1


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--start-directory",
        type=Path,
        default=ROOT / "tests",
        help="directory in which test discovery starts",
    )
    parser.add_argument("--pattern", default="test*.py")
    parser.add_argument("--verbosity", type=int, choices=(0, 1, 2), default=2)
    args = parser.parse_args(argv)

    start = args.start_directory.resolve()
    if not start.is_dir():
        parser.error(f"test directory does not exist: {start}")
    suite = unittest.defaultTestLoader.discover(
        str(start), pattern=args.pattern, top_level_dir=str(start)
    )
    result = unittest.TextTestRunner(verbosity=args.verbosity).run(suite)
    return result_exit_code(result)


if __name__ == "__main__":
    raise SystemExit(main())
