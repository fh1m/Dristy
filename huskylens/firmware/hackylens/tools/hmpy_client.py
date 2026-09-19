"""Synchronous reference client for the HackyLens HMPY v1 session layer."""

from __future__ import annotations

import struct
import time
from dataclasses import dataclass
from typing import Any, Callable, Protocol

from hmpy_protocol import (
    ErrorCode,
    Frame,
    FrameFlag,
    LINE_HANDSHAKE,
    LINE_READY,
    MessageType,
    ProtocolDecodeError,
    StreamDecoder,
    crc32,
    encode_frame,
)


NAME_MAX = 63
FILE_MAX = 256 * 1024
UPLOAD_DATA_MAX = 1016
RUN_TIME_MAX_MS = 300_000
FORMAT_TOKEN = b"ERASE USERFS"
CAPABILITIES_V1_REQUIRED = 0xFF
CAP_BOOT_FLAGS = 1 << 8
BOOT_FLAG_WDT1_RECOVERY = 1 << 0
DEFAULT_CONNECT_TIMEOUT = 8.0
HANDSHAKE_RETRY_INTERVAL = 0.25

HELLO_FIXED = struct.Struct("<BBBBIHHHHIII")
STATUS_FIXED = struct.Struct("<BBBBIIIIQQQII")


class ByteTransport(Protocol):
    def read(self, size: int = 1) -> bytes: ...
    def write(self, data: bytes) -> int | None: ...


def open_serial_transport(
    serial_module: Any,
    port: str,
    baud: int,
    *,
    timeout: float,
    write_timeout: float,
) -> ByteTransport:
    """Open pyserial without asserting DTR or RTS during configuration.

    Passing ``port`` to ``serial.Serial`` opens it immediately, before callers
    can select safe modem-control levels.  Some CP210x boards turn that edge
    into a reset.  Configure a closed instance first so the initial open uses
    deasserted DTR/RTS.
    """

    transport = serial_module.Serial()
    transport.port = port
    transport.baudrate = baud
    transport.timeout = timeout
    transport.write_timeout = write_timeout
    transport.dtr = False
    transport.rts = False
    try:
        transport.open()
    except BaseException:
        close = getattr(transport, "close", None)
        if callable(close):
            close()
        raise
    return transport


class HmpyClientError(RuntimeError):
    pass


class HmpyTimeout(HmpyClientError):
    pass


class HmpyRemoteError(HmpyClientError):
    def __init__(self, code: ErrorCode | int, detail: str = ""):
        try:
            self.code = ErrorCode(code)
        except ValueError:
            self.code = int(code)
        self.detail = detail
        label = self.code.name if isinstance(self.code, ErrorCode) else str(self.code)
        super().__init__(f"{label}: {detail}" if detail else label)


@dataclass(frozen=True, slots=True)
class HelloInfo:
    protocol_version: int
    filesystem_state: int
    runtime_state: int
    boot_flags: int
    capabilities: int
    max_payload: int
    upload_chunk: int
    name_max: int
    source_max: int
    file_max: int
    filesystem_total: int
    filesystem_used: int
    firmware_version: str
    board: str


@dataclass(frozen=True, slots=True)
class FileInfo:
    name: str
    size: int
    startup: bool = False


@dataclass(frozen=True, slots=True)
class RuntimeStatus:
    runtime_state: int
    exit_reason: int
    filesystem_state: int
    filesystem_error: int
    run_id: int
    source_bytes: int
    output_pending: int
    output_dropped: int
    started_us: int
    heartbeat_us: int
    deadline_us: int
    filesystem_total: int
    filesystem_used: int


@dataclass(frozen=True, slots=True)
class HmpyEvent:
    type: MessageType
    run_id: int = 0
    sequence: int = 0
    data: bytes = b""
    state: int | None = None
    exit_reason: int | None = None
    dropped_stream: int | None = None
    dropped_count: int = 0
    file_operation: int | None = None
    file_name: str = ""


def pack_name(name: str, *, allow_empty: bool = False) -> bytes:
    if not isinstance(name, str):
        raise TypeError("name must be str")
    encoded = name.encode("utf-8")
    if (not encoded and not allow_empty) or len(encoded) > NAME_MAX:
        raise ValueError("invalid HMPY name length")
    return bytes((len(encoded),)) + encoded


def unpack_name(payload: bytes, offset: int = 0) -> tuple[str, int]:
    if offset >= len(payload):
        raise HmpyClientError("missing HMPY name length")
    length = payload[offset]
    offset += 1
    if length > NAME_MAX or length > len(payload) - offset:
        raise HmpyClientError("invalid HMPY name")
    try:
        name = payload[offset : offset + length].decode("utf-8")
    except UnicodeDecodeError as exc:
        raise HmpyClientError("invalid UTF-8 name") from exc
    return name, offset + length


class HmpyClient:
    def __init__(
        self,
        transport: ByteTransport,
        *,
        timeout: float = 5.0,
        connect_timeout: float = DEFAULT_CONNECT_TIMEOUT,
        handshake_interval: float = HANDSHAKE_RETRY_INTERVAL,
        event_handler: Callable[[HmpyEvent], None] | None = None,
    ) -> None:
        self.transport = transport
        self.timeout = timeout
        self.connect_timeout = connect_timeout
        self.handshake_interval = handshake_interval
        self.event_handler = event_handler
        self.decoder = StreamDecoder()
        self.request_id = 0
        self.active = False
        self.events: list[HmpyEvent] = []

    def _write_all(self, data: bytes) -> None:
        position = 0
        while position < len(data):
            written = self.transport.write(data[position:])
            if written is None:
                written = len(data) - position
            if written <= 0:
                raise HmpyClientError("serial write made no progress")
            position += written

    def open(self, timeout: float | None = None) -> None:
        if self.active:
            return
        connect_timeout = self.connect_timeout if timeout is None else timeout
        if connect_timeout <= 0:
            raise ValueError("connect timeout must be positive")
        if self.handshake_interval <= 0:
            raise ValueError("handshake interval must be positive")
        reset = getattr(self.transport, "reset_input_buffer", None)
        if callable(reset):
            reset()
        started = time.monotonic()
        deadline = started + connect_timeout
        self._write_all(LINE_HANDSHAKE)
        next_handshake = started + self.handshake_interval
        line = bytearray()
        while time.monotonic() < deadline:
            data = self.transport.read(1)
            if data:
                for byte in data:
                    if byte in (10, 13):
                        if line == LINE_READY.rstrip(b"\n"):
                            self.decoder.reset()
                            self.active = True
                            return
                        line.clear()
                        continue
                    if len(line) < 256:
                        line.append(byte)
                    else:
                        line.clear()

            now = time.monotonic()
            if now >= deadline:
                break
            if now >= next_handshake:
                self._write_all(LINE_HANDSHAKE)
                next_handshake = now + self.handshake_interval
            elif not data:
                time.sleep(min(0.002, deadline - now, next_handshake - now))
        raise HmpyTimeout("device did not enter HMPY mode")

    def _next_request_id(self) -> int:
        self.request_id = (self.request_id + 1) & 0xFFFFFFFF
        if self.request_id == 0:
            self.request_id = 1
        return self.request_id

    @staticmethod
    def _remote_error(frame: Frame) -> HmpyRemoteError:
        if len(frame.payload) < 4:
            return HmpyRemoteError(ErrorCode.INTERNAL, "malformed error envelope")
        code, detail_length = struct.unpack_from("<HH", frame.payload)
        if detail_length != len(frame.payload) - 4:
            return HmpyRemoteError(ErrorCode.INTERNAL, "malformed error detail")
        detail = frame.payload[4:].decode("utf-8", errors="replace")
        return HmpyRemoteError(code, detail)

    def _decode_event(self, frame: Frame) -> HmpyEvent:
        payload = frame.payload
        if frame.type in (MessageType.STDOUT, MessageType.STDERR):
            if len(payload) < 8:
                raise HmpyClientError("short output event")
            run_id, sequence = struct.unpack_from("<II", payload)
            return HmpyEvent(frame.type, run_id, sequence, payload[8:])
        if frame.type is MessageType.STATE:
            if len(payload) != 14:
                raise HmpyClientError("invalid state event")
            state, exit_reason, run_id, heartbeat = struct.unpack("<BBIQ", payload)
            return HmpyEvent(
                frame.type,
                run_id=run_id,
                sequence=heartbeat,
                state=state,
                exit_reason=exit_reason,
            )
        if frame.type is MessageType.DROPPED:
            if len(payload) != 9:
                raise HmpyClientError("invalid dropped event")
            stream, run_id, count = struct.unpack("<BII", payload)
            return HmpyEvent(
                frame.type,
                run_id=run_id,
                dropped_stream=stream,
                dropped_count=count,
            )
        if frame.type is MessageType.FILE_CHANGED:
            if len(payload) < 2:
                raise HmpyClientError("invalid file event")
            operation = payload[0]
            name, end = unpack_name(payload, 1)
            if end != len(payload):
                raise HmpyClientError("trailing file event data")
            return HmpyEvent(
                frame.type,
                file_operation=operation,
                file_name=name,
            )
        raise HmpyClientError(f"unsupported event {frame.type.name}")

    def _emit_event(self, frame: Frame) -> None:
        event = self._decode_event(frame)
        self.events.append(event)
        if self.event_handler:
            self.event_handler(event)

    def request(
        self,
        message_type: MessageType,
        payload: bytes = b"",
        *,
        timeout: float | None = None,
    ) -> list[Frame]:
        if not self.active:
            raise HmpyClientError("HMPY session is not open")
        request_id = self._next_request_id()
        self._write_all(encode_frame(Frame(message_type, request_id=request_id, payload=payload)))
        deadline = time.monotonic() + (self.timeout if timeout is None else timeout)
        responses: list[Frame] = []

        while time.monotonic() < deadline:
            data = self.transport.read(256)
            if not data:
                time.sleep(0.002)
                continue
            completed = False
            for event in self.decoder.feed(data):
                if event.error is not None:
                    raise event.error
                frame = event.frame
                assert frame is not None
                if frame.request_id == 0:
                    self._emit_event(frame)
                    continue
                if frame.request_id != request_id or frame.type is not message_type:
                    raise HmpyClientError("unexpected HMPY response")
                if not frame.flags & FrameFlag.RESPONSE:
                    raise HmpyClientError("response flag missing")
                if frame.flags & FrameFlag.ERROR:
                    raise self._remote_error(frame)
                responses.append(frame)
                if not frame.flags & FrameFlag.MORE:
                    completed = True
            if completed:
                return responses
        raise HmpyTimeout(f"timeout waiting for {message_type.name}")

    def poll(self, duration: float = 0.0) -> list[HmpyEvent]:
        deadline = time.monotonic() + max(0.0, duration)
        first = True
        start = len(self.events)
        while first or time.monotonic() < deadline:
            first = False
            data = self.transport.read(256)
            if not data:
                if duration <= 0:
                    break
                time.sleep(0.002)
                continue
            for decoded in self.decoder.feed(data):
                if decoded.error is not None:
                    raise decoded.error
                frame = decoded.frame
                assert frame is not None
                if frame.request_id != 0:
                    raise HmpyClientError("unsolicited response")
                self._emit_event(frame)
        return self.events[start:]

    def hello(self) -> HelloInfo:
        payload = self.request(MessageType.HELLO)[0].payload
        if len(payload) < HELLO_FIXED.size + 2:
            raise HmpyClientError("short HELLO response")
        values = HELLO_FIXED.unpack_from(payload)
        offset = HELLO_FIXED.size
        version, offset = unpack_name(payload, offset)
        board, offset = unpack_name(payload, offset)
        if offset != len(payload):
            raise HmpyClientError("trailing HELLO response data")
        return HelloInfo(
            protocol_version=values[0],
            filesystem_state=values[1],
            runtime_state=values[2],
            boot_flags=values[3],
            capabilities=values[4],
            max_payload=values[5],
            upload_chunk=values[6],
            name_max=values[7],
            source_max=values[8],
            file_max=values[9],
            filesystem_total=values[10],
            filesystem_used=values[11],
            firmware_version=version,
            board=board,
        )

    def list_files(self) -> list[FileInfo]:
        result: list[FileInfo] = []
        for frame in self.request(MessageType.LIST):
            if not frame.payload:
                continue
            name, offset = unpack_name(frame.payload)
            if len(frame.payload) - offset != 4:
                raise HmpyClientError("invalid LIST entry")
            size = struct.unpack_from("<I", frame.payload, offset)[0]
            result.append(FileInfo(name, size))
        return result

    def stat(self, name: str) -> FileInfo:
        payload = self.request(MessageType.STAT, pack_name(name))[0].payload
        if len(payload) != 5:
            raise HmpyClientError("invalid STAT response")
        size, startup = struct.unpack("<IB", payload)
        return FileInfo(name, size, bool(startup))

    def read_file(self, name: str, offset: int = 0, length: int = 0) -> bytes:
        if not 0 <= offset <= 0xFFFFFFFF or not 0 <= length <= 0xFFFFFFFF:
            raise ValueError("read range out of bounds")
        frames = self.request(
            MessageType.READ,
            pack_name(name) + struct.pack("<II", offset, length),
            timeout=max(self.timeout, 35.0),
        )
        output = bytearray()
        expected = offset
        total: int | None = None
        for frame in frames:
            if len(frame.payload) < 8:
                raise HmpyClientError("invalid READ response")
            chunk_offset, chunk_total = struct.unpack_from("<II", frame.payload)
            if chunk_offset != expected or (total is not None and total != chunk_total):
                raise HmpyClientError("non-contiguous READ response")
            chunk_length = len(frame.payload) - 8
            if chunk_total < offset or chunk_length > chunk_total - chunk_offset:
                raise HmpyClientError("READ response exceeds file size")
            total = chunk_total
            output.extend(frame.payload[8:])
            expected += chunk_length
        if total is None:
            raise HmpyClientError("missing READ response")
        available = total - offset
        expected_length = available if length == 0 else min(length, available)
        if len(output) != expected_length:
            raise HmpyClientError("truncated READ response")
        return bytes(output)

    def upload(self, name: str, data: bytes) -> None:
        content = bytes(data)
        if len(content) > FILE_MAX:
            raise ValueError("file exceeds HMPY v1 limit")
        response = self.request(
            MessageType.UPLOAD_BEGIN,
            pack_name(name) + struct.pack("<II", len(content), crc32(content)),
        )[0].payload
        if len(response) != 8:
            raise HmpyClientError("invalid UPLOAD_BEGIN response")
        upload_id, next_offset = struct.unpack("<II", response)
        try:
            while next_offset < len(content):
                chunk = content[next_offset : next_offset + UPLOAD_DATA_MAX]
                ack = self.request(
                    MessageType.UPLOAD_CHUNK,
                    struct.pack("<II", upload_id, next_offset) + chunk,
                )[0].payload
                if len(ack) != 8:
                    raise HmpyClientError("invalid UPLOAD_CHUNK response")
                ack_id, acknowledged = struct.unpack("<II", ack)
                if ack_id != upload_id or acknowledged != next_offset + len(chunk):
                    raise HmpyClientError("invalid upload acknowledgement")
                next_offset = acknowledged
            self.request(MessageType.UPLOAD_COMMIT, struct.pack("<I", upload_id))
        except BaseException:
            try:
                self.request(MessageType.UPLOAD_ABORT, struct.pack("<I", upload_id))
            except (HmpyClientError, ProtocolDecodeError):
                pass
            raise

    def delete(self, name: str) -> None:
        self.request(MessageType.DELETE, pack_name(name))

    def set_startup(self, name: str | None) -> None:
        self.request(MessageType.SET_STARTUP, pack_name(name or "", allow_empty=True))

    def format_userfs(self, confirmation: str) -> None:
        if confirmation.encode("ascii", errors="strict") != FORMAT_TOKEN:
            raise ValueError(f"confirmation must be {FORMAT_TOKEN.decode()!r}")
        self.request(MessageType.FORMAT, FORMAT_TOKEN, timeout=max(self.timeout, 15.0))

    def run(self, name: str | None = None, time_limit_ms: int = 0) -> int:
        if not 0 <= time_limit_ms <= RUN_TIME_MAX_MS:
            raise ValueError("invalid run time limit")
        payload = pack_name(name or "", allow_empty=True) + struct.pack("<I", time_limit_ms)
        response = self.request(MessageType.RUN, payload)[0].payload
        if len(response) != 4:
            raise HmpyClientError("invalid RUN response")
        return struct.unpack("<I", response)[0]

    def stop(self) -> None:
        self.request(MessageType.STOP)

    def status(self) -> RuntimeStatus:
        payload = self.request(MessageType.STATUS)[0].payload
        if len(payload) != STATUS_FIXED.size:
            raise HmpyClientError("invalid STATUS response")
        return RuntimeStatus(*STATUS_FIXED.unpack(payload))

    def ping(self, payload: bytes = b"") -> bytes:
        response = self.request(MessageType.PING, bytes(payload))[0].payload
        if response != payload:
            raise HmpyClientError("PING echo mismatch")
        return response

    def close(self) -> None:
        if not self.active:
            return
        try:
            self.request(MessageType.SESSION_CLOSE)
        finally:
            self.active = False
            self.decoder.reset()
