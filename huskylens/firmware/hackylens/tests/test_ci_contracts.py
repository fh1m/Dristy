from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


def load_tool(name: str):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / f"{name}.py")
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class CiContractsTest(unittest.TestCase):
    def test_release_workflow_native_commands_are_fail_fast(self) -> None:
        workflow = (ROOT / ".github" / "workflows" / "release.yml").read_text(
            encoding="utf-8"
        )
        required = (
            "python tools/ci_diff_range.py",
            "git diff --check $diffRange",
            "python tools/check_docs.py",
            "python tools/check_phase2_evidence.py",
            "python tools/check_arch.py",
            "python tools/check_board_ports.py --all",
            "python tools/check_board_ports.py --board sipeed-maix-cube --compile",
            "python tools/check_env.py",
            "python tools/run_tests.py",
            "python tools/build_firmware.py full --board huskylens-sen0305 --disable-app micropython",
            "python tools/check_arch.py --verify-build-profile micropython-disabled",
            "python tools/check_phase2_evidence.py --verify-profile micropython-disabled",
            "python tools/check_firmware_symbols.py build/huskylens-sen0305/sdk-full/hackylens_full --expect absent",
            "python tools/build_firmware.py full --board huskylens-sen0305",
            "python tools/check_arch.py --verify-build-profile full",
            "python tools/check_phase2_evidence.py --verify-profile full",
            "python tools/check_firmware_symbols.py build/huskylens-sen0305/sdk-full/hackylens_full --expect present",
            'python tools/package_release.py --board huskylens-sen0305 --tag "${{ github.ref_name }}"',
        )
        self.assertIn("function Invoke-NativeChecked", workflow)
        self.assertIn("if ($LASTEXITCODE -ne 0)", workflow)
        for command in required:
            with self.subTest(command=command):
                self.assertIn(f"Invoke-NativeChecked {command}", workflow)
                self.assertNotRegex(
                    workflow,
                    re.compile(rf"(?m)^\s+{re.escape(command)}\s*$"),
                )

    def test_strict_runner_rejects_skips(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hackylens-strict-tests-") as temp:
            tests = Path(temp)
            source = tests / "test_sample.py"
            source.write_text(
                "import unittest\n"
                "class Sample(unittest.TestCase):\n"
                "    def test_skip(self):\n"
                "        self.skipTest('coverage unavailable')\n",
                encoding="utf-8",
            )
            result = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "tools" / "run_tests.py"),
                    "--start-directory",
                    str(tests),
                ],
                cwd=ROOT,
                text=True,
                capture_output=True,
                timeout=30,
            )
        self.assertEqual(result.returncode, 1)
        self.assertIn("strict test run skipped 1 test(s)", result.stderr)

    def test_strict_runner_accepts_complete_suite(self) -> None:
        with tempfile.TemporaryDirectory(prefix="hackylens-strict-tests-") as temp:
            tests = Path(temp)
            (tests / "test_sample.py").write_text(
                "import unittest\n"
                "class Sample(unittest.TestCase):\n"
                "    def test_pass(self):\n"
                "        self.assertTrue(True)\n",
                encoding="utf-8",
            )
            result = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "tools" / "run_tests.py"),
                    "--start-directory",
                    str(tests),
                ],
                cwd=ROOT,
                text=True,
                capture_output=True,
                timeout=30,
            )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_firmware_repo_does_not_build_or_package_the_standalone_ide(self) -> None:
        workflow = (ROOT / ".github" / "workflows" / "release.yml").read_text(
            encoding="utf-8"
        )
        packager = (ROOT / "tools" / "package_release.py").read_text(
            encoding="utf-8"
        )
        self.assertNotIn("bootstrap_ide", workflow)
        self.assertNotIn("hackylens-code", workflow)
        self.assertNotIn("actions/setup-node", workflow)
        self.assertNotIn("--ide", packager)
        self.assertNotIn("code.zip", packager)

    def test_micropython_patches_keep_lf_on_windows_runners(self) -> None:
        attributes = (ROOT / ".gitattributes").read_text(encoding="utf-8")
        self.assertIn(
            "firmware/third_party/micropython/patches/*.patch text eol=lf",
            attributes.splitlines(),
        )
        self.assertIn(
            "docs/evidence/phase2-*.json text eol=lf",
            attributes.splitlines(),
        )

    def test_release_workflow_validates_every_change_and_publishes_tags_only(self) -> None:
        workflow = (ROOT / ".github" / "workflows" / "release.yml").read_text(
            encoding="utf-8"
        )
        self.assertIn("  pull_request:\n", workflow)
        self.assertIn('      - "**"\n', workflow)
        self.assertIn("python tools/check_docs.py", workflow)
        self.assertIn("python tools/check_phase2_evidence.py", workflow)
        self.assertIn("python tools/run_tests.py", workflow)
        self.assertNotIn("msys2/setup-msys2", workflow)
        self.assertNotIn("update: true", workflow)
        self.assertIn('HOST_GCC_VERSION: "13.2.0"', workflow)
        self.assertIn("Configure pinned host GCC", workflow)
        self.assertIn("-dumpfullversion -dumpversion", workflow)
        self.assertIn("x86_64-w64-mingw32", workflow)
        self.assertIn(
            "full --board huskylens-sen0305 --disable-app micropython", workflow
        )
        disabled_build = workflow.index(
            "Invoke-NativeChecked python tools/build_firmware.py full --board huskylens-sen0305 --disable-app micropython"
        )
        disabled_verify = workflow.index(
            "Invoke-NativeChecked python tools/check_phase2_evidence.py --verify-profile micropython-disabled"
        )
        disabled_symbols = workflow.index(
            "Invoke-NativeChecked python tools/check_firmware_symbols.py build/huskylens-sen0305/sdk-full/hackylens_full --expect absent"
        )
        self.assertLess(disabled_build, disabled_verify)
        self.assertLess(disabled_verify, disabled_symbols)
        full_build = workflow.index(
            "Invoke-NativeChecked python tools/build_firmware.py full --board huskylens-sen0305\n"
        )
        full_verify = workflow.index(
            "Invoke-NativeChecked python tools/check_phase2_evidence.py --verify-profile full"
        )
        full_symbols = workflow.index(
            "Invoke-NativeChecked python tools/check_firmware_symbols.py build/huskylens-sen0305/sdk-full/hackylens_full --expect present"
        )
        self.assertLess(full_build, full_verify)
        self.assertLess(full_verify, full_symbols)
        self.assertIn("check_board_ports.py --all", workflow)
        self.assertIn(
            "check_board_ports.py --board sipeed-maix-cube --compile", workflow
        )
        self.assertNotIn("check_phase1_resources.py --board huskylens-sen0305", workflow)
        self.assertNotIn("--write-result docs/evidence/phase1-result.json", workflow)
        self.assertEqual(workflow.count("fetch-depth: 0"), 2)
        self.assertNotIn("\n          git diff --check\n", workflow)
        self.assertIn("PR_BASE_SHA: ${{ github.event.pull_request.base.sha }}", workflow)
        self.assertIn("PUSH_BEFORE_SHA: ${{ github.event.before }}", workflow)
        self.assertIn("DEFAULT_BRANCH: ${{ github.event.repository.default_branch }}", workflow)
        self.assertIn("python tools/ci_diff_range.py", workflow)
        self.assertNotIn("git rev-parse HEAD^", workflow)
        self.assertIn("git diff --check $diffRange", workflow)
        self.assertIn("--expect absent", workflow)
        self.assertIn("--expect present", workflow)
        self.assertGreaterEqual(
            workflow.count("github.event_name == 'push' && github.ref_type == 'tag'"),
            3,
        )
        self.assertIn("path: dist/release/", workflow)
        self.assertIn("dist/release/*", workflow)
        self.assertIn("hashFiles('tools/bootstrap_deps.py')", workflow)

    def test_host_gcc_dependency_is_version_and_content_pinned(self) -> None:
        bootstrap = load_tool("bootstrap_deps")
        self.assertEqual(bootstrap.HOST_GCC_VERSION, "13.2.0")
        self.assertEqual(bootstrap.HOST_GCC_MACHINE, "x86_64-w64-mingw32")
        self.assertIn("/13.2.0-rt_v11-rev0/", bootstrap.HOST_GCC_URL)
        self.assertRegex(bootstrap.HOST_GCC_SHA256, re.compile(r"^[0-9a-f]{64}$"))

    def test_readme_uses_only_qualified_firmware_artifacts(self) -> None:
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        self.assertNotRegex(readme, re.compile(r"build[\\/][^`\s]*\.bin"))
        self.assertIn("dist\\hackylens-full-huskylens-sen0305.bin", readme)
        self.assertIn("dist\\release\\hackylens-huskylens-sen0305-v<version>.*", readme)
        self.assertNotIn("`firmware/src/drivers`, `board`, `hal`", readme)
        self.assertIn("| `boards` |", readme)
        self.assertIn("`platforms/k210/hal`, `platforms/k210/startup`", readme)

    def test_zero_before_and_dispatch_ranges_cover_multi_commit_new_branch(self) -> None:
        selector = load_tool("ci_diff_range")
        with tempfile.TemporaryDirectory(prefix="hackylens-diff-range-") as directory:
            repository = Path(directory)
            subprocess.run(
                ["git", "init", "--quiet", "--initial-branch=main"],
                cwd=repository,
                check=True,
            )
            environment = {
                **os.environ,
                "GIT_AUTHOR_NAME": "CI Range Test",
                "GIT_AUTHOR_EMAIL": "range@example.invalid",
                "GIT_COMMITTER_NAME": "CI Range Test",
                "GIT_COMMITTER_EMAIL": "range@example.invalid",
            }

            def commit(name: str, content: str) -> str:
                (repository / name).write_text(content, encoding="utf-8")
                subprocess.run(["git", "add", name], cwd=repository, check=True)
                subprocess.run(
                    ["git", "commit", "--quiet", "-m", name],
                    cwd=repository, env=environment, check=True,
                )
                return subprocess.run(
                    ["git", "rev-parse", "HEAD"], cwd=repository,
                    text=True, capture_output=True, check=True,
                ).stdout.strip()

            commit("root.txt", "root\n")
            default_tip = commit("main.txt", "main\n")
            subprocess.run(
                ["git", "update-ref", "refs/remotes/origin/main", default_tip],
                cwd=repository, check=True,
            )
            subprocess.run(
                ["git", "switch", "--quiet", "-c", "feature"],
                cwd=repository, check=True,
            )
            commit("first.txt", "first\n")
            commit("second.txt", "second\n")
            expected = f"{default_tip}..HEAD"
            for event in ("push", "workflow_dispatch"):
                with self.subTest(event=event):
                    selected = selector.select_range(
                        event,
                        push_before_sha=selector.ZERO_SHA,
                        default_branch="main",
                        root=repository,
                    )
                    self.assertEqual(selected, expected)
                    changed = subprocess.run(
                        ["git", "diff", "--name-only", selected],
                        cwd=repository, text=True, capture_output=True, check=True,
                    ).stdout.splitlines()
                    self.assertEqual(changed, ["first.txt", "second.txt"])


if __name__ == "__main__":
    unittest.main()
