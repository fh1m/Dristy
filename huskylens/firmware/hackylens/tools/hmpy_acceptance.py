#!/usr/bin/env python3
"""Repeatable hardware acceptance runner for HackyLens HMPY v1.

The default invocation is a read-only probe.  Storage/runtime mutation,
formatting and lease-expiry reconnect are explicit opt-ins.  WDT recovery is
verified read-only against a probe report captured before a separately
documented physical fault-injection reset.  The core accepts an injected
session factory so the complete control flow can be tested without a serial
port.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import secrets
import sys
import time
from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable, Protocol

from hmpy_client import (
    BOOT_FLAG_WDT1_RECOVERY,
    CAPABILITIES_V1_REQUIRED,
    CAP_BOOT_FLAGS,
    DEFAULT_CONNECT_TIMEOUT,
    FORMAT_TOKEN,
    HmpyClient,
    HmpyEvent,
    HmpyRemoteError,
    open_serial_transport,
)
from hmpy_protocol import ErrorCode, MessageType


ROOT = Path(__file__).resolve().parents[1]
FORMAT_CONFIRMATION = FORMAT_TOKEN.decode("ascii")
EXPECTED_BOARD = "huskylens-sen0305"
EXPECTED_FILESYSTEM_TOTAL = 0x00390000
TRANSPORT_PING_COUNT = 1
TRANSPORT_PING_PAYLOAD = bytes(range(256)) * 4

FS_UNSUPPORTED = 1
FS_UNFORMATTED = 2
FS_CORRUPT = 3
FS_IO_ERROR = 4
FS_MOUNTED = 5

RUNTIME_STOPPED = 0
RUNTIME_STARTING = 1
RUNTIME_RUNNING = 2
RUNTIME_STOPPING = 3
RUNTIME_FINISHED = 4
RUNTIME_ERROR = 5
ACTIVE_STATES = {RUNTIME_STARTING, RUNTIME_RUNNING, RUNTIME_STOPPING}

EXIT_NONE = 0
EXIT_COMPLETE = 1
EXIT_REQUESTED = 2


class AcceptanceError(RuntimeError):
    """A failed acceptance invariant."""


class NeedsAction(AcceptanceError):
    """A safe precondition that requires an explicit operator action."""


class Clock(Protocol):
    def monotonic(self) -> float: ...
    def sleep(self, seconds: float) -> None: ...


class RealClock:
    def monotonic(self) -> float:
        return time.monotonic()

    def sleep(self, seconds: float) -> None:
        time.sleep(seconds)


class ClientLike(Protocol):
    def hello(self) -> Any: ...
    def list_files(self) -> list[Any]: ...
    def stat(self, name: str) -> Any: ...
    def read_file(self, name: str, offset: int = 0, length: int = 0) -> bytes: ...
    def upload(self, name: str, data: bytes) -> None: ...
    def delete(self, name: str) -> None: ...
    def set_startup(self, name: str | None) -> None: ...
    def format_userfs(self, confirmation: str) -> None: ...
    def run(self, name: str | None = None, time_limit_ms: int = 0) -> int: ...
    def stop(self) -> None: ...
    def status(self) -> Any: ...
    def ping(self, payload: bytes = b"") -> bytes: ...
    def poll(self, duration: float = 0.0) -> list[HmpyEvent]: ...
    def request(self, message_type: MessageType, payload: bytes = b"", **kwargs: Any) -> Any: ...
    def close(self) -> None: ...


class SessionFactory(Protocol):
    def connect(self, event_handler: Callable[[HmpyEvent], None]) -> ClientLike: ...
    def close(self, client: ClientLike, *, graceful: bool) -> None: ...


class SerialSessionFactory:
    """Creates real sessions.  pyserial is imported only when connect() runs."""

    def __init__(
        self, port: str, baud: int, timeout: float, connect_timeout: float
    ) -> None:
        self.port = port
        self.baud = baud
        self.timeout = timeout
        self.connect_timeout = connect_timeout

    def connect(self, event_handler: Callable[[HmpyEvent], None]) -> HmpyClient:
        try:
            import serial
        except ImportError as exc:  # pragma: no cover - depends on host setup
            raise AcceptanceError(
                "pyserial is required: python -m pip install pyserial"
            ) from exc
        transport = open_serial_transport(
            serial,
            self.port,
            self.baud,
            timeout=0.05,
            write_timeout=self.timeout,
        )
        client = HmpyClient(
            transport,
            timeout=self.timeout,
            connect_timeout=self.connect_timeout,
            event_handler=event_handler,
        )
        try:
            client.open()
        except BaseException:
            transport.close()
            raise
        return client

    def close(self, client: ClientLike, *, graceful: bool) -> None:
        transport = getattr(client, "transport", None)
        try:
            if graceful:
                client.close()
        finally:
            if transport is not None:
                close = getattr(transport, "close", None)
                if callable(close):
                    close()


@dataclass(slots=True)
class AcceptanceConfig:
    port: str
    baud: int = 115200
    timeout: float = 2.0
    connect_timeout: float = DEFAULT_CONNECT_TIMEOUT
    expected_version: str = ""
    expected_board: str = EXPECTED_BOARD
    workflow: bool = False
    lease_reconnect: bool = False
    format_userfs: bool = False
    confirm_format: str | None = None
    verify_wdt_recovery: bool = False
    wdt_baseline: Path | None = None
    report: Path | None = None
    namespace: str | None = None
    poll_interval: float = 0.05
    marker_timeout: float = 5.0
    stop_timeout: float = 2.0
    lease_wait_seconds: float = 11.0
    reconnect_timeout: float = 20.0
    recovery_observe_seconds: float = 3.0

    def validate(self) -> None:
        if self.verify_wdt_recovery and (
            self.format_userfs or self.workflow or self.lease_reconnect
        ):
            raise NeedsAction(
                "--verify-wdt-recovery is read-only and cannot be combined with "
                "--format-userfs, --workflow, or --lease-reconnect"
            )
        if self.format_userfs:
            if self.confirm_format != FORMAT_CONFIRMATION:
                raise NeedsAction(
                    f"format requires --confirm-format {FORMAT_CONFIRMATION!r}"
                )
        elif self.confirm_format is not None:
            raise NeedsAction("--confirm-format requires --format-userfs")
        if self.verify_wdt_recovery:
            if self.wdt_baseline is None:
                raise NeedsAction(
                    "WDT recovery verification requires --wdt-baseline from a "
                    "read-only probe captured before the separate physical reset"
                )
        elif self.wdt_baseline is not None:
            raise NeedsAction(
                "--wdt-baseline requires --verify-wdt-recovery"
            )
        if self.timeout <= 0 or self.connect_timeout <= 0 or self.stop_timeout <= 0:
            raise AcceptanceError("timeouts must be positive")
        if self.lease_reconnect and self.lease_wait_seconds <= 10.0:
            raise AcceptanceError("lease reconnect wait must exceed the 10 second lease")


@dataclass(slots=True)
class StepResult:
    name: str
    status: str
    duration_ms: int
    evidence: dict[str, Any] = field(default_factory=dict)
    error: str = ""


@dataclass(slots=True)
class Baseline:
    files: dict[str, int]
    startup: str | None


class EventTrace:
    """Collects events and de-duplicates replayed output after reconnect."""

    def __init__(self) -> None:
        self.events: list[HmpyEvent] = []
        self._streams: dict[tuple[int, int], bytearray] = {}
        self._base: dict[tuple[int, int], int] = {}
        self._next: dict[tuple[int, int], int] = {}
        self._pending_drop: dict[tuple[int, int], int] = {}
        self.sequence_errors: list[str] = []
        self.dropped: list[HmpyEvent] = []

    @staticmethod
    def _stream_type(stream: int | None) -> int:
        return int(MessageType.STDOUT if stream == 1 else MessageType.STDERR)

    @staticmethod
    def _sequence_key(event_type: MessageType, run_id: int) -> tuple[int, int]:
        # Runtime stdout has an independent sequence epoch for every run.
        # Firmware diagnostics use one absolute counter for the entire boot,
        # even though each event is tagged with the current runtime run ID.
        return (int(event_type), 0 if event_type is MessageType.STDERR else run_id)

    def handle(self, event: HmpyEvent) -> None:
        self.events.append(event)
        if event.type is MessageType.DROPPED:
            self.dropped.append(event)
            stream_type = MessageType(self._stream_type(event.dropped_stream))
            key = self._sequence_key(stream_type, event.run_id)
            self._pending_drop[key] = self._pending_drop.get(key, 0) + event.dropped_count
            return
        if event.type not in (MessageType.STDOUT, MessageType.STDERR):
            return
        key = self._sequence_key(event.type, event.run_id)
        start = event.sequence
        data = bytes(event.data)
        if key not in self._streams:
            self._streams[key] = bytearray()
            # Python stdout is reset to offset zero for every run. Firmware
            # diagnostics are a boot-global ring: a new HMPY session snapshots
            # its current absolute cursor, so the first STDERR event observed
            # in this boot epoch legitimately starts at a non-zero offset.
            base = start if event.type is MessageType.STDERR else 0
            self._base[key] = base
            self._next[key] = base
            if event.type is MessageType.STDERR:
                # A preceding DROPPED event describes bytes before this
                # session baseline. Keep it as acceptance evidence, but do not
                # let its count explain an unrelated later diagnostic gap.
                self._pending_drop.pop(key, None)
        stream = self._streams[key]
        base = self._base[key]
        expected = self._next[key]

        # A replay may begin before the first diagnostic byte observed by this
        # client. Those earlier bytes were outside its session baseline and
        # cannot be compared; retain and validate the overlapping suffix.
        if start < base:
            skip = min(base - start, len(data))
            start += skip
            data = data[skip:]
            if not data:
                return
        if start < expected:
            overlap = min(expected - start, len(data))
            relative = start - base
            if (
                relative + overlap <= len(stream)
                and stream[relative : relative + overlap] != data[:overlap]
            ):
                self.sequence_errors.append(
                    f"run {event.run_id} {event.type.name}: replay mismatch at {start}"
                )
            if overlap == len(data):
                return
            data = data[overlap:]
            start += overlap
        if start > expected:
            gap = start - expected
            pending = self._pending_drop.get(key, 0)
            if pending < gap:
                self.sequence_errors.append(
                    f"run {event.run_id} {event.type.name}: unexplained gap {gap}"
                )
            self._pending_drop[key] = max(0, pending - gap)
        relative = start - base
        if relative > len(stream):
            # Keep absolute offsets aligned after a reported or unexplained
            # gap. Any DROPPED event still makes assert_clean_output fail.
            stream.extend(b"\0" * (relative - len(stream)))
        if relative < len(stream):
            overlap = min(len(stream) - relative, len(data))
            if stream[relative : relative + overlap] != data[:overlap]:
                self.sequence_errors.append(
                    f"run {event.run_id} {event.type.name}: overlap mismatch at {start}"
                )
            data = data[overlap:]
            relative += overlap
            start += overlap
        stream.extend(data)
        self._next[key] = max(expected, start + len(data))

    def stdout(self, run_id: int) -> bytes:
        return bytes(self._streams.get((int(MessageType.STDOUT), run_id), b""))

    def event_index(self) -> int:
        return len(self.events)

    def new_boot(self) -> None:
        """Start a new sequence epoch after a proved device reset.

        Runtime run IDs and byte sequences restart at one/zero after WDT1, so
        they must not be mistaken for a same-boot reconnect replay.
        """

        self._streams.clear()
        self._base.clear()
        self._next.clear()
        self._pending_drop.clear()
        self.sequence_errors.clear()
        self.dropped.clear()

    def has_file_event(self, start: int, operation: int, name: str) -> bool:
        return any(
            event.type is MessageType.FILE_CHANGED
            and event.file_operation == operation
            and event.file_name == name
            for event in self.events[start:]
        )

    def assert_clean_output(self, run_id: int) -> None:
        errors = [error for error in self.sequence_errors if f"run {run_id} " in error]
        drops = [event for event in self.dropped if event.run_id == run_id]
        if errors:
            raise AcceptanceError("; ".join(errors))
        if drops:
            raise AcceptanceError(f"run {run_id} unexpectedly dropped output")


class AcceptanceRunner:
    def __init__(
        self,
        config: AcceptanceConfig,
        factory: SessionFactory,
        *,
        clock: Clock | None = None,
    ) -> None:
        self.config = config
        self.factory = factory
        self.clock = clock or RealClock()
        self.trace = EventTrace()
        self.client: ClientLike | None = None
        self.steps: list[StepResult] = []
        self.created: set[str] = set()
        self.baseline: Baseline | None = None
        self.hello_info: Any | None = None
        self.status_info: Any | None = None
        self.started = datetime.now(timezone.utc)
        nonce = config.namespace or secrets.token_hex(4)
        if not nonce or len(nonce) > 24 or any(c not in "abcdefghijklmnopqrstuvwxyz0123456789-" for c in nonce):
            raise ValueError("namespace must use lowercase ASCII letters, digits, and '-' only")
        self.namespace = nonce
        self.report_path = config.report or self._default_report_path()
        self.exit_code = 0

    def _default_report_path(self) -> Path:
        stamp = self.started.strftime("%Y%m%dT%H%M%SZ")
        return (
            ROOT
            / "build"
            / "hardware-acceptance"
            / f"hmpy-{stamp}-{self.namespace}.json"
        )

    def _name(self, role: str) -> str:
        name = f"hka-{self.namespace}-{role}.py"
        if len(name.encode("ascii")) > 63:
            raise AcceptanceError("acceptance filename exceeds HMPY name limit")
        return name

    def _client(self) -> ClientLike:
        if self.client is None:
            raise AcceptanceError("serial session is not connected")
        return self.client

    def _connect(self) -> ClientLike:
        self.client = self.factory.connect(self.trace.handle)
        return self.client

    def _close(self, *, graceful: bool) -> None:
        if self.client is None:
            return
        client = self.client
        self.client = None
        self.factory.close(client, graceful=graceful)

    def _run_step(self, name: str, operation: Callable[[], dict[str, Any] | None]) -> None:
        started = self.clock.monotonic()
        try:
            evidence = operation() or {}
        except NeedsAction as exc:
            self.steps.append(
                StepResult(name, "NEEDS_ACTION", int((self.clock.monotonic() - started) * 1000), error=str(exc))
            )
            self.exit_code = max(self.exit_code, 2)
            raise
        except BaseException as exc:
            self.steps.append(
                StepResult(name, "FAIL", int((self.clock.monotonic() - started) * 1000), error=f"{type(exc).__name__}: {exc}")
            )
            self.exit_code = 1
            raise
        self.steps.append(
            StepResult(name, "PASS", int((self.clock.monotonic() - started) * 1000), evidence=evidence)
        )

    def _probe(self) -> dict[str, Any]:
        client = self._client()
        hello = client.hello()
        self.hello_info = hello
        if hello.protocol_version != 1:
            raise AcceptanceError(f"protocol version is {hello.protocol_version}, expected 1")
        if self.config.expected_version and hello.firmware_version != self.config.expected_version:
            raise AcceptanceError(
                f"firmware is {hello.firmware_version!r}, expected {self.config.expected_version!r}"
            )
        if hello.board != self.config.expected_board:
            raise AcceptanceError(f"board is {hello.board!r}, expected {self.config.expected_board!r}")
        if hello.capabilities & CAPABILITIES_V1_REQUIRED != CAPABILITIES_V1_REQUIRED:
            raise AcceptanceError(f"missing v1 capabilities: 0x{hello.capabilities:08x}")
        expected_limits = (1024, 1016, 63, 65535, 256 * 1024)
        actual_limits = (
            hello.max_payload,
            hello.upload_chunk,
            hello.name_max,
            hello.source_max,
            hello.file_max,
        )
        if actual_limits != expected_limits:
            raise AcceptanceError(f"unexpected HMPY limits: {actual_limits!r}")
        status = client.status()
        self.status_info = status

        payload = TRANSPORT_PING_PAYLOAD
        for ping_number in range(1, TRANSPORT_PING_COUNT + 1):
            if client.ping(payload) != payload:
                raise AcceptanceError(f"PING echo mismatch at attempt {ping_number}")
        return {
            "hello": asdict(hello),
            "status": asdict(status),
            "ping_count": TRANSPORT_PING_COUNT,
            "ping_payload_bytes": len(payload),
            "ping_payload_sha256": hashlib.sha256(payload).hexdigest(),
        }

    def _filesystem_probe(self) -> dict[str, Any]:
        hello = self.hello_info
        status = self.status_info
        if hello is None or status is None:
            raise AcceptanceError("transport probe evidence is unavailable")
        if status.filesystem_state != hello.filesystem_state:
            raise AcceptanceError("HELLO/STATUS filesystem state mismatch")
        if status.filesystem_total != hello.filesystem_total:
            raise AcceptanceError("HELLO/STATUS filesystem size mismatch")
        if hello.filesystem_state == FS_MOUNTED and hello.filesystem_total != EXPECTED_FILESYSTEM_TOTAL:
            raise AcceptanceError(f"mounted userfs size is 0x{hello.filesystem_total:x}")
        if hello.filesystem_total not in (0, EXPECTED_FILESYSTEM_TOTAL):
            raise AcceptanceError(f"unexpected userfs size 0x{hello.filesystem_total:x}")
        if hello.filesystem_used > hello.filesystem_total:
            raise AcceptanceError("filesystem used bytes exceed total")
        if hello.filesystem_state == FS_UNFORMATTED:
            if not self.config.format_userfs:
                raise NeedsAction(
                    "userfs is unformatted; rerun with --format-userfs "
                    f"--confirm-format {FORMAT_CONFIRMATION!r}"
                )
        elif hello.filesystem_state != FS_MOUNTED:
            if not self.config.format_userfs or hello.filesystem_state == FS_UNSUPPORTED:
                raise AcceptanceError(f"userfs state is {hello.filesystem_state}")
        if status.runtime_state in ACTIVE_STATES:
            raise NeedsAction(
                f"runtime is active (state={status.runtime_state}); stop it before acceptance"
            )

        client = self._client()
        files = []
        file_sizes: dict[str, int] = {}
        startup: str | None = None
        if hello.filesystem_state == FS_MOUNTED:
            files = client.list_files()
            for entry in files:
                info = client.stat(entry.name)
                file_sizes[entry.name] = entry.size
                if info.startup:
                    if startup is not None:
                        raise AcceptanceError("multiple startup files reported")
                    startup = entry.name
        return {
            "file_count": len(files),
            "files": file_sizes,
            "startup": startup,
            "format_required": hello.filesystem_state != FS_MOUNTED,
        }

    def _wait_file_event(self, start: int, operation: int, name: str, timeout: float = 2.0) -> None:
        deadline = self.clock.monotonic() + timeout
        while self.clock.monotonic() < deadline:
            if self.trace.has_file_event(start, operation, name):
                return
            self._client().poll(self.config.poll_interval)
            self.clock.sleep(self.config.poll_interval)
        raise AcceptanceError(f"missing FILE_CHANGED op={operation} name={name!r}")

    def _format(self) -> dict[str, Any]:
        client = self._client()
        mounted = client.hello().filesystem_state == FS_MOUNTED
        before = (
            {entry.name: entry.size for entry in client.list_files()}
            if mounted
            else None
        )
        try:
            client.request(MessageType.FORMAT, b"NOT CONFIRMED")
        except HmpyRemoteError as exc:
            if exc.code != ErrorCode.CONFIRMATION_REQUIRED:
                raise AcceptanceError(f"wrong FORMAT error: {exc.code}") from exc
        else:
            raise AcceptanceError("device accepted an invalid FORMAT token")
        if before is not None and {
            entry.name: entry.size for entry in client.list_files()
        } != before:
            raise AcceptanceError("invalid FORMAT token changed userfs")
        event_start = self.trace.event_index()
        client.format_userfs(FORMAT_CONFIRMATION)
        self._wait_file_event(event_start, 4, "")
        hello = client.hello()
        if hello.filesystem_state != FS_MOUNTED:
            raise AcceptanceError(f"format left userfs in state {hello.filesystem_state}")
        if hello.filesystem_total != EXPECTED_FILESYSTEM_TOTAL:
            raise AcceptanceError(f"format exposed userfs size 0x{hello.filesystem_total:x}")
        if client.list_files():
            raise AcceptanceError("formatted userfs is not empty")
        return {
            "erased_file_count": len(before) if before is not None else 0,
            "filesystem_total": hello.filesystem_total,
        }

    def _snapshot(self) -> Baseline:
        client = self._client()
        entries = client.list_files()
        startup: str | None = None
        files: dict[str, int] = {}
        for entry in entries:
            info = client.stat(entry.name)
            files[entry.name] = entry.size
            if info.startup:
                if startup is not None:
                    raise AcceptanceError("multiple startup files reported")
                startup = entry.name
        return Baseline(files, startup)

    def _upload(self, name: str, source: bytes) -> None:
        if self.baseline is not None and name in self.baseline.files:
            raise NeedsAction(
                f"acceptance fixture {name!r} already exists; choose a new run namespace"
            )
        event_start = self.trace.event_index()
        # The name is unique to this runner.  Track it before the RPC so a
        # response timeout after a successful atomic publish is still cleaned.
        self.created.add(name)
        self._client().upload(name, source)
        self._wait_file_event(event_start, 1, name)

    def _wait_marker(self, run_id: int, marker: bytes) -> None:
        deadline = self.clock.monotonic() + self.config.marker_timeout
        ping_at = self.clock.monotonic() + 2.0
        while self.clock.monotonic() < deadline:
            if marker in self.trace.stdout(run_id):
                return
            self._client().poll(self.config.poll_interval)
            if self.clock.monotonic() >= ping_at:
                self._client().ping(b"wait")
                ping_at = self.clock.monotonic() + 2.0
            self.clock.sleep(self.config.poll_interval)
        raise AcceptanceError(f"run {run_id} did not print marker {marker!r}")

    def _wait_status(self, run_id: int, timeout: float, *, active: bool = False) -> Any:
        deadline = self.clock.monotonic() + timeout
        last = None
        while self.clock.monotonic() < deadline:
            last = self._client().status()
            if last.run_id == run_id:
                is_active = last.runtime_state in ACTIVE_STATES
                if is_active == active:
                    return last
            self.clock.sleep(self.config.poll_interval)
        detail = asdict(last) if last is not None else None
        raise AcceptanceError(f"run {run_id} state timeout; last={detail!r}")

    def _run_complete(self, name: str, marker: bytes) -> tuple[int, Any]:
        run_id = self._client().run(name, 5000)
        self._wait_marker(run_id, marker)
        status = self._wait_status(run_id, self.config.marker_timeout, active=False)
        if status.runtime_state != RUNTIME_FINISHED or status.exit_reason != EXIT_COMPLETE:
            raise AcceptanceError(
                f"run {run_id} ended state={status.runtime_state} exit={status.exit_reason}"
            )
        self.trace.assert_clean_output(run_id)
        return run_id, status

    def _run_stop(self, name: str, marker: bytes, unreachable: bytes = b"") -> dict[str, Any]:
        run_id = self._client().run(name, 10000)
        self._wait_marker(run_id, marker)
        started = self.clock.monotonic()
        self._client().stop()
        status = self._wait_status(run_id, self.config.stop_timeout, active=False)
        elapsed = self.clock.monotonic() - started
        if status.runtime_state != RUNTIME_STOPPED or status.exit_reason != EXIT_REQUESTED:
            raise AcceptanceError(
                f"STOP run {run_id} ended state={status.runtime_state} exit={status.exit_reason}"
            )
        if elapsed >= self.config.stop_timeout:
            raise AcceptanceError(f"STOP exceeded {self.config.stop_timeout:.3f}s")
        output = self.trace.stdout(run_id)
        if unreachable and unreachable in output:
            raise AcceptanceError(f"run {run_id} reached code after native loop")
        self.trace.assert_clean_output(run_id)
        return {"run_id": run_id, "stop_ms": int(elapsed * 1000)}

    def _workflow(self) -> dict[str, Any]:
        client = self._client()
        token = self.namespace.upper()
        main_name = self._name("main")
        coop_name = self._name("coop")
        sum_name = self._name("sum")
        min_name = self._name("min")
        done = f"HKA-{token}-DONE".encode()
        main_source = (
            f'print("HKA-{token}-BEGIN")\n'
            f'for i in range(3): print("HKA-{token}-LOG", i)\n'
            f'print("HKA-{token}-DONE")\n'
            "#" + "x" * 2300 + "\n"
        ).encode()
        if len(main_source) <= 2 * 1016:
            raise AcceptanceError("storage fixture does not span more than two upload chunks")
        self._upload(main_name, main_source)
        listed = {entry.name: entry.size for entry in client.list_files()}
        if listed.get(main_name) != len(main_source):
            raise AcceptanceError("uploaded file missing from LIST")
        info = client.stat(main_name)
        if info.size != len(main_source) or info.startup:
            raise AcceptanceError("unexpected uploaded STAT")
        content = client.read_file(main_name)
        if content != main_source:
            raise AcceptanceError("READ did not reproduce uploaded source")
        if client.read_file(main_name, 997, 1200) != main_source[997:2197]:
            raise AcceptanceError("ranged READ mismatch")

        event_start = self.trace.event_index()
        client.set_startup(main_name)
        self._wait_file_event(event_start, 3, main_name)
        if not client.stat(main_name).startup:
            raise AcceptanceError("SET_STARTUP not reflected by STAT")
        startup_run = client.run(None, 5000)
        self._wait_marker(startup_run, done)
        startup_status = self._wait_status(startup_run, self.config.marker_timeout, active=False)
        if startup_status.runtime_state != RUNTIME_FINISHED or startup_status.exit_reason != EXIT_COMPLETE:
            raise AcceptanceError("startup RUN did not complete")
        self.trace.assert_clean_output(startup_run)
        client.set_startup(None)
        if client.stat(main_name).startup:
            raise AcceptanceError("startup selection did not clear")

        fixtures = (
            (
                coop_name,
                f'print("HKA-{token}-COOP-READY")\nwhile True:\n    pass\n'.encode(),
                f"HKA-{token}-COOP-READY".encode(),
                b"",
                "cooperative",
            ),
            (
                sum_name,
                f'consume = sum\nitems = range(1 << 60)\nprint("HKA-{token}-SUM-READY")\nconsume(items)\nprint("HKA-{token}-UNREACHABLE")\n'.encode(),
                f"HKA-{token}-SUM-READY".encode(),
                f"HKA-{token}-UNREACHABLE".encode(),
                "sum",
            ),
            (
                min_name,
                f'consume = min\nitems = range(1 << 60)\nprint("HKA-{token}-MIN-READY")\nconsume(items)\nprint("HKA-{token}-UNREACHABLE")\n'.encode(),
                f"HKA-{token}-MIN-READY".encode(),
                f"HKA-{token}-UNREACHABLE".encode(),
                "min",
            ),
        )
        stop_evidence: dict[str, Any] = {}
        for name, source, marker, unreachable, label in fixtures:
            self._upload(name, source)
            stop_evidence[label] = self._run_stop(name, marker, unreachable)
        reuse_run, _ = self._run_complete(main_name, done)
        return {
            "upload_bytes": len(main_source),
            "upload_sha256": hashlib.sha256(main_source).hexdigest(),
            "startup_run_id": startup_run,
            "stop": stop_evidence,
            "reuse_run_id": reuse_run,
        }

    def _lease_reconnect(self) -> dict[str, Any]:
        token = self.namespace.upper()
        name = self._name("lease")
        ready = f"HKA-{token}-LEASE-READY".encode()
        source = (
            "import hackylens as hl\n"
            f'print("HKA-{token}-LEASE-READY")\n'
            "hl.sleep_ms(20000)\n"
            f'print("HKA-{token}-LEASE-DONE")\n'
        ).encode()
        self._upload(name, source)
        run_id = self._client().run(name, 30000)
        self._wait_marker(run_id, ready)
        self._close(graceful=False)
        self.clock.sleep(self.config.lease_wait_seconds)
        self._reconnect_until(self.config.reconnect_timeout)
        hello = self._client().hello()
        if self.hello_info and hello.firmware_version != self.hello_info.firmware_version:
            raise AcceptanceError("firmware version changed across lease reconnect")
        status = self._client().status()
        if status.run_id != run_id or status.runtime_state != RUNTIME_RUNNING:
            raise AcceptanceError(
                f"run did not survive lease reconnect: run={status.run_id} state={status.runtime_state}"
            )
        if self._client().ping(b"reconnected") != b"reconnected":
            raise AcceptanceError("post-reconnect PING failed")
        stopped = self._run_stop_existing(run_id)
        self.trace.assert_clean_output(run_id)
        return {
            "run_id": run_id,
            "firmware_version": hello.firmware_version,
            "stop_ms": stopped,
        }

    def _run_stop_existing(self, run_id: int) -> int:
        started = self.clock.monotonic()
        self._client().stop()
        status = self._wait_status(run_id, self.config.stop_timeout, active=False)
        if status.runtime_state != RUNTIME_STOPPED or status.exit_reason != EXIT_REQUESTED:
            raise AcceptanceError("reconnected run did not stop cleanly")
        return int((self.clock.monotonic() - started) * 1000)

    def _reconnect_until(self, timeout: float) -> None:
        deadline = self.clock.monotonic() + timeout
        last_error: BaseException | None = None
        while self.clock.monotonic() < deadline:
            try:
                self._connect()
                return
            except BaseException as exc:
                last_error = exc
                self.client = None
                self.clock.sleep(min(0.25, self.config.poll_interval))
        raise AcceptanceError(f"device did not reconnect: {last_error}")

    def _load_wdt_baseline(self) -> tuple[dict[str, Any], dict[str, int], str | None]:
        path = self.config.wdt_baseline
        if path is None:
            raise NeedsAction("missing WDT baseline report")
        try:
            report = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError, TypeError) as exc:
            raise NeedsAction(f"cannot read WDT baseline report {path}: {exc}") from exc
        if report.get("result") != "PASS" or report.get("exit_code") != 0:
            raise NeedsAction("WDT baseline must be a successful acceptance report")
        requested = report.get("requested")
        if not isinstance(requested, dict) or any(
            bool(requested.get(flag))
            for flag in (
                "workflow",
                "lease_reconnect",
                "format_userfs",
                "verify_wdt_recovery",
            )
        ):
            raise NeedsAction("WDT baseline must come from the default read-only probe")
        probe = next(
            (
                step
                for step in report.get("steps", [])
                if step.get("name") == "probe" and step.get("status") == "PASS"
            ),
            None,
        )
        if not isinstance(probe, dict) or not isinstance(probe.get("evidence"), dict):
            raise NeedsAction("baseline report has no successful probe evidence")
        evidence = probe["evidence"]
        hello = evidence.get("hello")
        filesystem = next(
            (
                step
                for step in report.get("steps", [])
                if step.get("name") == "filesystem" and step.get("status") == "PASS"
            ),
            None,
        )
        filesystem_evidence = (
            filesystem.get("evidence") if isinstance(filesystem, dict) else None
        )
        if not isinstance(filesystem_evidence, dict):
            # Reports predating the split transport/filesystem probes stored
            # the mounted LIST/STAT baseline in the probe step itself.
            filesystem_evidence = evidence
        files = filesystem_evidence.get("files")
        startup = filesystem_evidence.get("startup")
        if not isinstance(hello, dict) or not isinstance(files, dict):
            raise NeedsAction("baseline report predates WDT recovery evidence fields")
        if int(hello.get("boot_flags", 0)) & BOOT_FLAG_WDT1_RECOVERY:
            raise NeedsAction("baseline was captured during an earlier WDT recovery boot")
        try:
            normalized_files = {str(name): int(size) for name, size in files.items()}
        except (TypeError, ValueError) as exc:
            raise NeedsAction("baseline report contains invalid file metadata") from exc
        if startup is not None and not isinstance(startup, str):
            raise NeedsAction("baseline report contains invalid startup metadata")
        if not startup:
            raise NeedsAction(
                "WDT autostart-suppression evidence requires a selected startup script"
            )
        return hello, normalized_files, startup

    def _verify_wdt_recovery(self) -> dict[str, Any]:
        if not self.hello_info or not (self.hello_info.capabilities & CAP_BOOT_FLAGS):
            raise AcceptanceError("firmware does not advertise boot flags")
        baseline_hello, baseline_files, baseline_startup = self._load_wdt_baseline()
        hello = self.hello_info
        if not (hello.boot_flags & BOOT_FLAG_WDT1_RECOVERY):
            raise NeedsAction(
                "current boot is not WDT1 recovery; perform the separately documented "
                "physical fault-injection reset, then rerun this read-only check"
            )
        if hello.firmware_version != baseline_hello.get("firmware_version"):
            raise AcceptanceError("firmware version changed across WDT reset")
        if hello.board != baseline_hello.get("board"):
            raise AcceptanceError("board identity changed across WDT reset")
        status = self._client().status()
        if status.runtime_state != RUNTIME_STOPPED or status.exit_reason != EXIT_NONE or status.run_id != 0:
            raise AcceptanceError(
                f"unsafe WDT recovery state={status.runtime_state} exit={status.exit_reason} run={status.run_id}"
            )
        current = self._snapshot()
        if current.files != baseline_files:
            raise AcceptanceError(
                f"WDT recovery file mismatch: before={baseline_files!r} after={current.files!r}"
            )
        if current.startup != baseline_startup:
            raise AcceptanceError(
                f"WDT recovery startup mismatch: before={baseline_startup!r} after={current.startup!r}"
            )
        deadline = self.clock.monotonic() + self.config.recovery_observe_seconds
        while self.clock.monotonic() < deadline:
            observed = self._client().status()
            if observed.runtime_state in ACTIVE_STATES or observed.run_id != 0:
                raise AcceptanceError("startup script ran during WDT recovery boot")
            self.clock.sleep(self.config.poll_interval)
        return {
            "boot_flags": hello.boot_flags,
            "baseline_report": str(self.config.wdt_baseline),
            "file_count": len(current.files),
            "startup": current.startup or "",
        }

    def _restore_startup(self) -> None:
        if self.baseline is None:
            return
        self._client().set_startup(self.baseline.startup)

    def _cleanup(self) -> dict[str, Any]:
        if self.baseline is None:
            return {"created": 0}
        client = self._client()
        status = client.status()
        if status.runtime_state in ACTIVE_STATES:
            client.stop()
            self._wait_status(status.run_id, max(self.config.stop_timeout, 2.1), active=False)
        self._restore_startup()
        for name in sorted(self.created):
            try:
                client.delete(name)
            except HmpyRemoteError as exc:
                if exc.code != ErrorCode.NOT_FOUND:
                    raise
        final = self._snapshot()
        if final.files != self.baseline.files:
            raise AcceptanceError(
                f"cleanup file mismatch: before={self.baseline.files!r} after={final.files!r}"
            )
        if final.startup != self.baseline.startup:
            raise AcceptanceError(
                f"cleanup startup mismatch: before={self.baseline.startup!r} after={final.startup!r}"
            )
        return {"deleted": len(self.created), "restored_startup": self.baseline.startup or ""}

    def execute(self) -> int:
        primary_error: BaseException | None = None
        mutated = self.config.workflow or self.config.lease_reconnect
        try:
            self._run_step("configuration", lambda: (self.config.validate(), {})[1])
            self._run_step("connect", lambda: (self._connect(), {"port": self.config.port})[1])
            self._run_step("probe", self._probe)
            self._run_step("filesystem", self._filesystem_probe)
            if self.config.format_userfs:
                self._run_step("format", self._format)
            if mutated:
                self.baseline = self._snapshot()
            if self.config.workflow:
                self._run_step("workflow", self._workflow)
            if self.config.lease_reconnect:
                self._run_step("lease_reconnect", self._lease_reconnect)
            if self.config.verify_wdt_recovery:
                self._run_step("wdt_recovery", self._verify_wdt_recovery)
        except BaseException as exc:
            primary_error = exc
        finally:
            if mutated and self.baseline is not None and self.client is not None:
                try:
                    self._run_step("cleanup", self._cleanup)
                except BaseException as cleanup_error:
                    if primary_error is None:
                        primary_error = cleanup_error
            if self.client is not None:
                try:
                    self._close(graceful=True)
                except BaseException as close_error:
                    self.steps.append(StepResult("close", "FAIL", 0, error=f"{type(close_error).__name__}: {close_error}"))
                    self.exit_code = 1
                    if primary_error is None:
                        primary_error = close_error
            self._write_report(primary_error)
        return self.exit_code

    def _write_report(self, error: BaseException | None) -> None:
        finished = datetime.now(timezone.utc)
        report = {
            "schema": 1,
            "started_at": self.started.isoformat(),
            "finished_at": finished.isoformat(),
            "duration_ms": int((finished - self.started).total_seconds() * 1000),
            "port": self.config.port,
            "baud": self.config.baud,
            "namespace": self.namespace,
            "requested": {
                "workflow": self.config.workflow,
                "lease_reconnect": self.config.lease_reconnect,
                "format_userfs": self.config.format_userfs,
                "verify_wdt_recovery": self.config.verify_wdt_recovery,
            },
            "result": "PASS" if self.exit_code == 0 else "NEEDS_ACTION" if self.exit_code == 2 else "FAIL",
            "exit_code": self.exit_code,
            "error": f"{type(error).__name__}: {error}" if error is not None else "",
            "steps": [asdict(step) for step in self.steps],
        }
        self.report_path.parent.mkdir(parents=True, exist_ok=True)
        self.report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def default_version() -> str:
    try:
        return (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    except OSError:
        return ""


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--port", required=True)
    result.add_argument("--baud", type=int, default=115200)
    result.add_argument("--timeout", type=float, default=2.0)
    result.add_argument(
        "--connect-timeout",
        type=float,
        default=DEFAULT_CONNECT_TIMEOUT,
        help="seconds to retry the line handshake while the board boots",
    )
    result.add_argument("--expected-version", default=default_version())
    result.add_argument("--workflow", action="store_true", help="run reversible storage/runtime tests")
    result.add_argument("--lease-reconnect", action="store_true", help="drop a live session and recover after lease expiry")
    result.add_argument("--format-userfs", action="store_true", help="DESTRUCTIVE: format userfs before tests")
    result.add_argument("--confirm-format", metavar="TOKEN")
    result.add_argument(
        "--verify-wdt-recovery",
        action="store_true",
        help="READ-ONLY: verify a separately triggered physical WDT1 recovery boot",
    )
    result.add_argument(
        "--wdt-baseline",
        type=Path,
        help="JSON report from a read-only probe captured before the physical reset",
    )
    result.add_argument("--report", type=Path)
    return result


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    config = AcceptanceConfig(
        port=args.port,
        baud=args.baud,
        timeout=args.timeout,
        connect_timeout=args.connect_timeout,
        expected_version=args.expected_version,
        workflow=args.workflow,
        lease_reconnect=args.lease_reconnect,
        format_userfs=args.format_userfs,
        confirm_format=args.confirm_format,
        verify_wdt_recovery=args.verify_wdt_recovery,
        wdt_baseline=args.wdt_baseline,
        report=args.report,
    )
    factory = SerialSessionFactory(
        config.port, config.baud, config.timeout, config.connect_timeout
    )
    runner = AcceptanceRunner(config, factory)
    code = runner.execute()
    result = "PASS" if code == 0 else "NEEDS_ACTION" if code == 2 else "FAIL"
    print(f"HMPY acceptance: {result}")
    print(f"report: {runner.report_path}")
    return code


if __name__ == "__main__":
    raise SystemExit(main())
