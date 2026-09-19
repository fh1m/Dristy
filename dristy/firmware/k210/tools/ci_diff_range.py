#!/usr/bin/env python3
"""Select the explicit commit range checked by CI's ``git diff --check``."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ZERO_SHA = "0" * 40


class DiffRangeError(RuntimeError):
    """A safe full CI change range cannot be determined."""


def _git(args: list[str], *, root: Path) -> str | None:
    result = subprocess.run(
        ["git", *args], cwd=root, text=True, capture_output=True
    )
    if result.returncode != 0:
        return None
    value = result.stdout.strip()
    return value or None


def _git_ok(args: list[str], *, root: Path) -> bool:
    return subprocess.run(
        ["git", *args], cwd=root, text=True, capture_output=True
    ).returncode == 0


def _default_branch_base(default_branch: str, *, root: Path) -> str | None:
    candidates = [
        f"refs/remotes/origin/{default_branch}",
        f"origin/{default_branch}",
        default_branch,
    ]
    for candidate in candidates:
        if not _git_ok(["rev-parse", "--verify", candidate], root=root):
            continue
        base = _git(["merge-base", candidate, "HEAD"], root=root)
        if base:
            return base
    return None


def _root_commit(*, root: Path) -> str:
    commits = _git(["rev-list", "--max-parents=0", "HEAD"], root=root)
    if not commits:
        raise DiffRangeError("cannot determine repository root commit")
    roots = commits.splitlines()
    if len(roots) != 1:
        raise DiffRangeError("cannot choose among multiple repository roots")
    return roots[0]


def select_range(
    event_name: str,
    *,
    pr_base_sha: str = "",
    push_before_sha: str = "",
    default_branch: str = "main",
    root: Path = ROOT,
) -> str:
    if event_name == "pull_request" and pr_base_sha:
        if not _git_ok(["cat-file", "-e", f"{pr_base_sha}^{{commit}}"], root=root):
            raise DiffRangeError("pull-request base commit is unavailable")
        return f"{pr_base_sha}...HEAD"
    if event_name == "push" and push_before_sha and push_before_sha != ZERO_SHA:
        if not _git_ok(["cat-file", "-e", f"{push_before_sha}^{{commit}}"], root=root):
            raise DiffRangeError("push-before commit is unavailable")
        return f"{push_before_sha}..HEAD"

    # A zero-before new branch/tag push and workflow_dispatch have no reliable
    # HEAD^ boundary: the event can introduce multiple commits. Prefer the
    # fetched default branch's merge base and otherwise include every commit
    # from the repository root (using the empty tree when HEAD is itself root).
    base = _default_branch_base(default_branch, root=root)
    if base:
        return f"{base}..HEAD"
    _root_commit(root=root)
    # Git's empty-tree object ID is invariant for SHA-1 repositories. Comparing
    # against it includes the root commit and every later commit.
    empty_tree = "4b825dc642cb6eb9a060e54bf8d69288fbee4904"
    return f"{empty_tree}..HEAD"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--event", default=os.environ.get("GITHUB_EVENT_NAME", ""))
    parser.add_argument("--pr-base", default=os.environ.get("PR_BASE_SHA", ""))
    parser.add_argument("--push-before", default=os.environ.get("PUSH_BEFORE_SHA", ""))
    parser.add_argument(
        "--default-branch",
        default=os.environ.get("DEFAULT_BRANCH", "main"),
    )
    args = parser.parse_args(argv)
    try:
        selected = select_range(
            args.event,
            pr_base_sha=args.pr_base,
            push_before_sha=args.push_before,
            default_branch=args.default_branch,
        )
    except DiffRangeError as exc:
        print(f"[ERR] {exc}", file=sys.stderr)
        return 2
    print(selected)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
