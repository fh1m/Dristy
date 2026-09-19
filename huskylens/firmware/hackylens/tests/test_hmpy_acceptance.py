from __future__ import annotations

import json
import re
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from hmpy_acceptance import (
    AcceptanceConfig,
    AcceptanceError,
    AcceptanceRunner,
    EventTrace,
    FORMAT_CONFIRMATION,
    TRANSPORT_PING_COUNT,
    TRANSPORT_PING_PAYLOAD,
    parser,
)
from hmpy_client import (
    BOOT_FLAG_WDT1_RECOVERY,
    CAP_BOOT_FLAGS,
    CAPABILITIES_V1_REQUIRED,
    FileInfo,
    HelloInfo,
    HmpyEvent,
    HmpyRemoteError,
    RuntimeStatus,
)
from hmpy_protocol import ErrorCode, MessageType


FS_ERROR_UNFORMATTED = 4
FS_ERROR_CORRUPT = 5


class FakeClock:
    def __init__(self) -> None:
        self.now = 0.0

    def monotonic(self) -> float:
        return self.now

    def sleep(self, seconds: float) -> None:
        self.now += max(seconds, 0.0001)


class FakeDevice:
    def __init__(self) -> None:
        self.files = {"user.py": b"print('user')\n"}
        self.startup = "user.py"
        self.filesystem_state = 5
        # Exercise the real edge where a previous missing .startup lookup may
        # leave last_error=NOT_FOUND while the filesystem remains mounted.
        self.filesystem_error = int(ErrorCode.NOT_FOUND)
        self.boot_flags = 0
        self.runtime_state = 0
        self.exit_reason = 0
        self.run_id = 0
        self.current_name = ""
        self.current_source = b""
        self.run_output: dict[int, bytes] = {}
        self.mutations: list[str] = []
        self.upload_sizes: list[int] = []
        self.operations: list[str] = []
        self.ping_payloads: list[bytes] = []


class FakeClient:
    def __init__(self, device: FakeDevice, handler) -> None:
        self.device = device
        self.handler = handler
        self.closed = False

    def _event(self, event: HmpyEvent) -> None:
        self.handler(event)

    def hello(self) -> HelloInfo:
        self.device.operations.append("HELLO")
        used = sum(len(data) for data in self.device.files.values())
        return HelloInfo(
            protocol_version=1,
            filesystem_state=self.device.filesystem_state,
            runtime_state=self.device.runtime_state,
            boot_flags=self.device.boot_flags,
            capabilities=CAPABILITIES_V1_REQUIRED | CAP_BOOT_FLAGS,
            max_payload=1024,
            upload_chunk=1016,
            name_max=63,
            source_max=65535,
            file_max=256 * 1024,
            filesystem_total=0x390000,
            filesystem_used=used,
            firmware_version="0.3.0",
            board="huskylens-sen0305",
        )

    def list_files(self) -> list[FileInfo]:
        self.device.operations.append("LIST")
        if self.device.filesystem_state != 5:
            raise HmpyRemoteError(ErrorCode.CONFIRMATION_REQUIRED, "unformatted")
        return [FileInfo(name, len(data)) for name, data in sorted(self.device.files.items())]

    def stat(self, name: str) -> FileInfo:
        self.device.operations.append(f"STAT:{name}")
        if name not in self.device.files:
            raise HmpyRemoteError(ErrorCode.NOT_FOUND, name)
        return FileInfo(name, len(self.device.files[name]), name == self.device.startup)

    def read_file(self, name: str, offset: int = 0, length: int = 0) -> bytes:
        data = self.device.files[name]
        end = len(data) if length == 0 else min(len(data), offset + length)
        return data[offset:end]

    def upload(self, name: str, data: bytes) -> None:
        self.device.files[name] = bytes(data)
        self.device.mutations.append(f"upload:{name}")
        self.device.upload_sizes.append(len(data))
        self._event(
            HmpyEvent(MessageType.FILE_CHANGED, file_operation=1, file_name=name)
        )

    def delete(self, name: str) -> None:
        if name not in self.device.files:
            raise HmpyRemoteError(ErrorCode.NOT_FOUND, name)
        del self.device.files[name]
        if self.device.startup == name:
            self.device.startup = ""
        self.device.mutations.append(f"delete:{name}")
        self._event(
            HmpyEvent(MessageType.FILE_CHANGED, file_operation=2, file_name=name)
        )

    def set_startup(self, name: str | None) -> None:
        selected = name or ""
        if selected and selected not in self.device.files:
            raise HmpyRemoteError(ErrorCode.NOT_FOUND, selected)
        self.device.startup = selected
        self.device.mutations.append(f"startup:{selected}")
        self._event(
            HmpyEvent(MessageType.FILE_CHANGED, file_operation=3, file_name=selected)
        )

    def format_userfs(self, confirmation: str) -> None:
        if confirmation != FORMAT_CONFIRMATION:
            raise ValueError("bad local token")
        self.device.files.clear()
        self.device.startup = ""
        self.device.filesystem_state = 5
        self.device.filesystem_error = 0
        self.device.mutations.append("format")
        self._event(HmpyEvent(MessageType.FILE_CHANGED, file_operation=4, file_name=""))

    @staticmethod
    def _printed_literals(source: bytes) -> list[bytes]:
        return re.findall(rb'print\("([^"]+)"', source)

    def _stdout(self, data: bytes) -> None:
        run_id = self.device.run_id
        old = self.device.run_output.get(run_id, b"")
        self.device.run_output[run_id] = old + data
        self._event(HmpyEvent(MessageType.STDOUT, run_id=run_id, sequence=len(old), data=data))

    def run(self, name: str | None = None, time_limit_ms: int = 0) -> int:
        selected = name or self.device.startup
        if selected not in self.device.files:
            raise HmpyRemoteError(ErrorCode.NOT_FOUND, selected)
        self.device.run_id += 1
        self.device.runtime_state = 2
        self.device.exit_reason = 0
        self.device.current_name = selected
        self.device.current_source = self.device.files[selected]
        self.device.mutations.append(f"run:{selected}")
        self._event(
            HmpyEvent(
                MessageType.STATE,
                run_id=self.device.run_id,
                state=2,
                exit_reason=0,
            )
        )
        literals = self._printed_literals(self.device.current_source)
        if literals:
            self._stdout(literals[0] + b"\n")
        blocking = (
            b"while True" in self.device.current_source
            or b"consume(items)" in self.device.current_source
            or b"sleep_ms(20000)" in self.device.current_source
        )
        if not blocking:
            for literal in literals[1:]:
                self._stdout(literal + b"\n")
            self.device.runtime_state = 4
            self.device.exit_reason = 1
            self._event(
                HmpyEvent(
                    MessageType.STATE,
                    run_id=self.device.run_id,
                    state=4,
                    exit_reason=1,
                )
            )
        return self.device.run_id

    def stop(self) -> None:
        if self.device.runtime_state not in (1, 2, 3):
            raise HmpyRemoteError(ErrorCode.NOT_RUNNING)
        self.device.runtime_state = 0
        self.device.exit_reason = 2
        self._event(
            HmpyEvent(
                MessageType.STATE,
                run_id=self.device.run_id,
                state=0,
                exit_reason=2,
            )
        )

    def status(self) -> RuntimeStatus:
        self.device.operations.append("STATUS")
        return RuntimeStatus(
            runtime_state=self.device.runtime_state,
            exit_reason=self.device.exit_reason,
            filesystem_state=self.device.filesystem_state,
            filesystem_error=self.device.filesystem_error,
            run_id=self.device.run_id,
            source_bytes=len(self.device.current_source),
            output_pending=0,
            output_dropped=0,
            started_us=1,
            heartbeat_us=2,
            deadline_us=3,
            filesystem_total=0x390000,
            filesystem_used=sum(len(data) for data in self.device.files.values()),
        )

    def ping(self, payload: bytes = b"") -> bytes:
        self.device.operations.append("PING")
        self.device.ping_payloads.append(bytes(payload))
        return bytes(payload)

    def poll(self, duration: float = 0.0) -> list[HmpyEvent]:
        return []

    def request(self, message_type: MessageType, payload: bytes = b"", **kwargs):
        if message_type is MessageType.FORMAT and payload != FORMAT_CONFIRMATION.encode():
            raise HmpyRemoteError(ErrorCode.CONFIRMATION_REQUIRED, FORMAT_CONFIRMATION)
        raise AssertionError((message_type, payload))

    def close(self) -> None:
        self.closed = True


class FakeFactory:
    def __init__(self, device: FakeDevice | None = None) -> None:
        self.device = device or FakeDevice()
        self.connect_count = 0
        self.closes: list[bool] = []

    def connect(self, event_handler) -> FakeClient:
        self.connect_count += 1
        client = FakeClient(self.device, event_handler)
        # A same-boot reconnect replays retained output from absolute offset 0.
        # The runner must de-duplicate this rather than flag a false gap.
        if self.connect_count > 1 and self.device.runtime_state == 2:
            output = self.device.run_output.get(self.device.run_id, b"")
            if output:
                event_handler(
                    HmpyEvent(
                        MessageType.STDOUT,
                        run_id=self.device.run_id,
                        sequence=0,
                        data=output,
                    )
                )
        return client

    def close(self, client: FakeClient, *, graceful: bool) -> None:
        self.closes.append(graceful)
        if graceful:
            client.close()


class HmpyAcceptanceTests(unittest.TestCase):
    def config(self, report: Path, **overrides) -> AcceptanceConfig:
        values = dict(
            port="FAKE",
            expected_version="0.3.0",
            report=report,
            namespace="unit",
            poll_interval=0.001,
            marker_timeout=0.2,
            stop_timeout=0.2,
            reconnect_timeout=0.2,
            recovery_observe_seconds=0.01,
        )
        values.update(overrides)
        return AcceptanceConfig(**values)

    def run_in_temp(self, device: FakeDevice | None = None, **overrides):
        temporary = tempfile.TemporaryDirectory()
        report = Path(temporary.name) / "acceptance.json"
        factory = FakeFactory(device)
        runner = AcceptanceRunner(
            self.config(report, **overrides), factory, clock=FakeClock()
        )
        code = runner.execute()
        payload = json.loads(report.read_text(encoding="utf-8"))
        return temporary, factory, runner, code, payload

    def test_connect_timeout_is_independent_and_long_enough_for_board_boot(self) -> None:
        args = parser().parse_args(["--port", "FAKE", "--timeout", "2.0"])
        self.assertEqual(args.timeout, 2.0)
        self.assertGreater(args.connect_timeout, 5.0)

        config = self.config(Path("unused.json"), connect_timeout=0.0)
        with self.assertRaises(AcceptanceError):
            config.validate()

    def test_default_probe_is_read_only_and_ignores_stale_last_error(self) -> None:
        temporary, factory, _runner, code, report = self.run_in_temp()
        self.addCleanup(temporary.cleanup)
        self.assertEqual(code, 0)
        self.assertEqual(factory.connect_count, 1)
        self.assertEqual(factory.device.mutations, [])
        self.assertEqual(report["result"], "PASS")
        self.assertEqual(
            [step["name"] for step in report["steps"]],
            ["configuration", "connect", "probe", "filesystem"],
        )
        probe = next(step for step in report["steps"] if step["name"] == "probe")
        self.assertEqual(probe["status"], "PASS")
        self.assertEqual(probe["evidence"]["hello"]["filesystem_state"], 5)
        self.assertEqual(probe["evidence"]["status"]["filesystem_state"], 5)
        self.assertEqual(probe["evidence"]["ping_count"], 1)
        self.assertEqual(probe["evidence"]["ping_payload_bytes"], 1024)
        self.assertEqual(
            probe["evidence"]["ping_payload_sha256"],
            "785b0751fc2c53dc14a4ce3d800e69ef9ce1009eb327ccf458afe09c242c26c9",
        )
        self.assertEqual(len(factory.device.ping_payloads), TRANSPORT_PING_COUNT)
        self.assertTrue(
            all(payload == TRANSPORT_PING_PAYLOAD for payload in factory.device.ping_payloads)
        )
        filesystem = next(
            step for step in report["steps"] if step["name"] == "filesystem"
        )
        self.assertEqual(filesystem["status"], "PASS")
        self.assertEqual(filesystem["evidence"]["files"], {"user.py": 14})
        self.assertEqual(filesystem["evidence"]["startup"], "user.py")
        self.assertEqual(
            factory.device.operations,
            ["HELLO", "STATUS", "PING", "LIST", "STAT:user.py"],
        )

    def test_corrupt_userfs_preserves_transport_evidence_before_failure(self) -> None:
        device = FakeDevice()
        device.filesystem_state = 3
        device.filesystem_error = FS_ERROR_CORRUPT
        device.files.clear()
        device.startup = ""
        temporary, factory, _runner, code, report = self.run_in_temp(device=device)
        self.addCleanup(temporary.cleanup)

        self.assertEqual(code, 1)
        self.assertEqual(report["result"], "FAIL")
        self.assertEqual(factory.device.mutations, [])
        self.assertEqual(
            [step["name"] for step in report["steps"]],
            ["configuration", "connect", "probe", "filesystem"],
        )
        probe, filesystem = report["steps"][-2:]
        self.assertEqual(probe["status"], "PASS")
        self.assertEqual(probe["evidence"]["hello"]["filesystem_state"], 3)
        self.assertEqual(
            probe["evidence"]["status"]["filesystem_error"], FS_ERROR_CORRUPT
        )
        self.assertEqual(probe["evidence"]["ping_count"], TRANSPORT_PING_COUNT)
        self.assertEqual(probe["evidence"]["ping_payload_bytes"], 1024)
        self.assertEqual(filesystem["status"], "FAIL")
        self.assertIn("userfs state is 3", filesystem["error"])
        self.assertEqual(factory.device.operations, ["HELLO", "STATUS", "PING"])

    def test_unformatted_userfs_preserves_transport_evidence_before_action(self) -> None:
        device = FakeDevice()
        device.filesystem_state = 2
        device.filesystem_error = FS_ERROR_UNFORMATTED
        device.files.clear()
        device.startup = ""
        temporary, factory, _runner, code, report = self.run_in_temp(device=device)
        self.addCleanup(temporary.cleanup)

        self.assertEqual(code, 2)
        self.assertEqual(report["result"], "NEEDS_ACTION")
        self.assertEqual(factory.device.mutations, [])
        probe, filesystem = report["steps"][-2:]
        self.assertEqual(probe["name"], "probe")
        self.assertEqual(probe["status"], "PASS")
        self.assertEqual(probe["evidence"]["hello"]["filesystem_state"], 2)
        self.assertEqual(probe["evidence"]["status"]["filesystem_state"], 2)
        self.assertEqual(probe["evidence"]["ping_count"], TRANSPORT_PING_COUNT)
        self.assertEqual(filesystem["name"], "filesystem")
        self.assertEqual(filesystem["status"], "NEEDS_ACTION")
        self.assertIn("--format-userfs", filesystem["error"])
        self.assertEqual(factory.device.operations, ["HELLO", "STATUS", "PING"])

    def test_bad_destructive_tokens_write_report_without_connecting(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report_path = Path(directory) / "bad-token.json"
            factory = FakeFactory()
            runner = AcceptanceRunner(
                self.config(
                    report_path,
                    format_userfs=True,
                    confirm_format="erase userfs",
                ),
                factory,
                clock=FakeClock(),
            )
            self.assertEqual(runner.execute(), 2)
            self.assertEqual(factory.connect_count, 0)
            report = json.loads(report_path.read_text(encoding="utf-8"))
            self.assertEqual(report["result"], "NEEDS_ACTION")

            baseline_report = Path(directory) / "bad-baseline.json"
            baseline_factory = FakeFactory()
            baseline_runner = AcceptanceRunner(
                self.config(
                    baseline_report,
                    wdt_baseline=report_path,
                ),
                baseline_factory,
                clock=FakeClock(),
            )
            self.assertEqual(baseline_runner.execute(), 2)
            self.assertEqual(baseline_factory.connect_count, 0)
            self.assertTrue(baseline_report.exists())

    def test_reversible_workflow_spans_chunks_stops_native_loops_and_restores(self) -> None:
        temporary, factory, _runner, code, report = self.run_in_temp(workflow=True)
        self.addCleanup(temporary.cleanup)
        self.assertEqual(code, 0, report)
        self.assertEqual(factory.device.files, {"user.py": b"print('user')\n"})
        self.assertEqual(factory.device.startup, "user.py")
        self.assertGreater(max(factory.device.upload_sizes), 2 * 1016)
        workflow = next(step for step in report["steps"] if step["name"] == "workflow")
        self.assertEqual(workflow["status"], "PASS")
        self.assertEqual(set(workflow["evidence"]["stop"]), {"cooperative", "sum", "min"})
        self.assertEqual(report["steps"][-1]["name"], "cleanup")

    def test_lease_reconnect_accepts_exact_output_replay_overlap(self) -> None:
        temporary, factory, _runner, code, report = self.run_in_temp(
            lease_reconnect=True,
            lease_wait_seconds=10.01,
        )
        self.addCleanup(temporary.cleanup)
        self.assertEqual(code, 0, report)
        self.assertEqual(factory.connect_count, 2)
        self.assertIn(False, factory.closes)
        self.assertEqual(factory.device.files, {"user.py": b"print('user')\n"})

    def test_format_requires_opt_in_and_passes_with_exact_token(self) -> None:
        temporary, factory, _runner, code, report = self.run_in_temp(
            format_userfs=True,
            confirm_format=FORMAT_CONFIRMATION,
        )
        self.addCleanup(temporary.cleanup)
        self.assertEqual(code, 0, report)
        self.assertEqual(factory.device.files, {})
        self.assertIn("format", factory.device.mutations)

    def test_wdt_recovery_is_read_only_postcheck_against_probe_baseline(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            baseline_path = Path(directory) / "baseline.json"
            verification_path = Path(directory) / "verification.json"
            device = FakeDevice()
            baseline_factory = FakeFactory(device)
            baseline_runner = AcceptanceRunner(
                self.config(baseline_path), baseline_factory, clock=FakeClock()
            )
            self.assertEqual(baseline_runner.execute(), 0)
            mutations_before = list(device.mutations)

            # A real operator performs the separately documented physical
            # fault-injection reset between these two read-only invocations.
            device.boot_flags = BOOT_FLAG_WDT1_RECOVERY
            device.runtime_state = 0
            device.exit_reason = 0
            device.run_id = 0
            verification_factory = FakeFactory(device)
            verification_runner = AcceptanceRunner(
                self.config(
                    verification_path,
                    verify_wdt_recovery=True,
                    wdt_baseline=baseline_path,
                ),
                verification_factory,
                clock=FakeClock(),
            )
            self.assertEqual(verification_runner.execute(), 0)
            report = json.loads(verification_path.read_text(encoding="utf-8"))
            recovery = next(
                step for step in report["steps"] if step["name"] == "wdt_recovery"
            )
            self.assertEqual(recovery["status"], "PASS")
            self.assertEqual(device.mutations, mutations_before)
            self.assertEqual(device.files, {"user.py": b"print('user')\n"})
            self.assertEqual(device.startup, "user.py")

    def test_wdt_postcheck_rejects_mutating_suite_combinations_before_connect(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report_path = Path(directory) / "combined.json"
            factory = FakeFactory()
            runner = AcceptanceRunner(
                self.config(
                    report_path,
                    workflow=True,
                    verify_wdt_recovery=True,
                    wdt_baseline=Path(directory) / "baseline.json",
                ),
                factory,
                clock=FakeClock(),
            )
            self.assertEqual(runner.execute(), 2)
            self.assertEqual(factory.connect_count, 0)
            report = json.loads(report_path.read_text(encoding="utf-8"))
            self.assertEqual(report["result"], "NEEDS_ACTION")

    def test_fixture_name_collision_never_overwrites_or_deletes_user_file(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report_path = Path(directory) / "collision.json"
            device = FakeDevice()
            collision = "hka-unit-main.py"
            original = b"print('keep me')\n"
            device.files[collision] = original
            factory = FakeFactory(device)
            runner = AcceptanceRunner(
                self.config(report_path, workflow=True), factory, clock=FakeClock()
            )
            self.assertEqual(runner.execute(), 2)
            self.assertEqual(device.files[collision], original)
            self.assertNotIn(f"upload:{collision}", device.mutations)
            self.assertNotIn(f"delete:{collision}", device.mutations)

    def test_explicit_format_can_recover_an_unformatted_userfs(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report_path = Path(directory) / "unformatted.json"
            device = FakeDevice()
            device.filesystem_state = 2
            device.files.clear()
            device.startup = ""
            factory = FakeFactory(device)
            runner = AcceptanceRunner(
                self.config(
                    report_path,
                    format_userfs=True,
                    confirm_format=FORMAT_CONFIRMATION,
                ),
                factory,
                clock=FakeClock(),
            )
            self.assertEqual(runner.execute(), 0)
            report = json.loads(report_path.read_text(encoding="utf-8"))
            filesystem = next(
                step for step in report["steps"] if step["name"] == "filesystem"
            )
            self.assertTrue(filesystem["evidence"]["format_required"])
            self.assertEqual(device.filesystem_state, 5)
            self.assertIn("format", device.mutations)

    def test_exact_format_opt_in_recovers_corrupt_but_never_unsupported(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            for state, expected_code, should_format in ((3, 0, True), (1, 1, False)):
                with self.subTest(filesystem_state=state):
                    report_path = Path(directory) / f"state-{state}.json"
                    device = FakeDevice()
                    device.filesystem_state = state
                    device.files.clear()
                    device.startup = ""
                    factory = FakeFactory(device)
                    runner = AcceptanceRunner(
                        self.config(
                            report_path,
                            format_userfs=True,
                            confirm_format=FORMAT_CONFIRMATION,
                        ),
                        factory,
                        clock=FakeClock(),
                    )
                    self.assertEqual(runner.execute(), expected_code)
                    self.assertEqual("format" in device.mutations, should_format)
                    report = json.loads(report_path.read_text(encoding="utf-8"))
                    probe = next(
                        step for step in report["steps"] if step["name"] == "probe"
                    )
                    self.assertEqual(probe["status"], "PASS")
                    filesystem = next(
                        step for step in report["steps"] if step["name"] == "filesystem"
                    )
                    self.assertEqual(
                        filesystem["status"], "PASS" if should_format else "FAIL"
                    )

    def test_event_trace_deduplicates_reconnect_overlap(self) -> None:
        trace = EventTrace()
        trace.handle(HmpyEvent(MessageType.STDOUT, run_id=7, sequence=0, data=b"abc"))
        trace.handle(HmpyEvent(MessageType.STDOUT, run_id=7, sequence=0, data=b"abc"))
        trace.handle(HmpyEvent(MessageType.STDOUT, run_id=7, sequence=2, data=b"cdef"))
        self.assertEqual(trace.stdout(7), b"abcdef")
        self.assertEqual(trace.sequence_errors, [])

    def test_event_trace_uses_first_stderr_offset_as_absolute_baseline(self) -> None:
        trace = EventTrace()
        trace.handle(
            HmpyEvent(MessageType.STDERR, run_id=1, sequence=9611, data=b"abc")
        )
        trace.handle(
            HmpyEvent(MessageType.STDERR, run_id=2, sequence=9614, data=b"def")
        )
        # A replay overlapping the retained diagnostic bytes is validated
        # relative to the non-zero absolute base.
        trace.handle(
            HmpyEvent(MessageType.STDERR, run_id=2, sequence=9612, data=b"bcdef")
        )
        self.assertEqual(trace.sequence_errors, [])

        trace.handle(
            HmpyEvent(MessageType.STDERR, run_id=3, sequence=9620, data=b"x")
        )
        self.assertIn("run 3 STDERR: unexplained gap 3", trace.sequence_errors)

        trace.new_boot()
        trace.handle(
            HmpyEvent(MessageType.STDERR, run_id=2, sequence=12000, data=b"new")
        )
        self.assertEqual(trace.sequence_errors, [])

    def test_event_trace_accounts_for_stderr_drop_but_still_fails_acceptance(self) -> None:
        trace = EventTrace()
        trace.handle(
            HmpyEvent(MessageType.STDERR, run_id=1, sequence=100, data=b"a")
        )
        trace.handle(
            HmpyEvent(
                MessageType.DROPPED,
                run_id=2,
                dropped_stream=2,
                dropped_count=2,
            )
        )
        trace.handle(
            HmpyEvent(MessageType.STDERR, run_id=2, sequence=103, data=b"b")
        )
        self.assertEqual(trace.sequence_errors, [])
        with self.assertRaisesRegex(AcceptanceError, "unexpectedly dropped output"):
            trace.assert_clean_output(2)

    def test_event_trace_still_requires_stdout_to_start_at_zero(self) -> None:
        trace = EventTrace()
        trace.handle(
            HmpyEvent(MessageType.STDOUT, run_id=3, sequence=5, data=b"late")
        )
        self.assertIn("run 3 STDOUT: unexplained gap 5", trace.sequence_errors)


if __name__ == "__main__":
    unittest.main()
