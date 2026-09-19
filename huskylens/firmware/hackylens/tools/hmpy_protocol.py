"""Reference codec for the HackyLens MicroPython protocol (HMPY) v1.

The codec has no serial-port dependency. ``encode_frame()`` returns one COBS
packet including its trailing zero delimiter. ``StreamDecoder`` accepts
arbitrarily fragmented byte chunks and recovers at the next delimiter after a
malformed or oversized packet.
"""

from __future__ import annotations

import binascii
import struct
from dataclasses import dataclass
from enum import IntEnum, IntFlag
from typing import Iterable


MAGIC = b"HMPY"
PROTOCOL_VERSION = 1
LINE_HANDSHAKE = b"HKMPROTO 1\n"
LINE_READY = b"HKMPROTO 1 READY\n"

HEADER = struct.Struct("<4sBBHII")
CRC = struct.Struct("<I")
HEADER_SIZE = HEADER.size
CRC_SIZE = CRC.size
RAW_OVERHEAD = HEADER_SIZE + CRC_SIZE
MAX_PAYLOAD_SIZE = 1024
MAX_RAW_FRAME = RAW_OVERHEAD + MAX_PAYLOAD_SIZE
MAX_ENCODED_PACKET = MAX_RAW_FRAME + MAX_RAW_FRAME // 254 + 1
MAX_WIRE_FRAME = MAX_ENCODED_PACKET + 1


class MessageType(IntEnum):
    HELLO = 0x01
    LIST = 0x02
    STAT = 0x03
    READ = 0x04

    UPLOAD_BEGIN = 0x10
    UPLOAD_CHUNK = 0x11
    UPLOAD_COMMIT = 0x12
    UPLOAD_ABORT = 0x13

    DELETE = 0x20
    SET_STARTUP = 0x21
    FORMAT = 0x22

    RUN = 0x30
    STOP = 0x31
    STATUS = 0x32

    PING = 0x40
    SESSION_CLOSE = 0x41

    STDOUT = 0x80
    STDERR = 0x81
    STATE = 0x82
    FILE_CHANGED = 0x83
    DROPPED = 0x84


class FrameFlag(IntFlag):
    RESPONSE = 0x0001
    ERROR = 0x0002
    MORE = 0x0004


FRAME_FLAG_MASK = FrameFlag.RESPONSE | FrameFlag.ERROR | FrameFlag.MORE


class ErrorCode(IntEnum):
    OK = 0
    INVALID_REQUEST = 1
    UNSUPPORTED_VERSION = 2
    UNSUPPORTED_TYPE = 3
    INVALID_PAYLOAD = 4
    NOT_FOUND = 5
    ALREADY_EXISTS = 6
    BUSY = 7
    PERMISSION_DENIED = 8
    NO_SPACE = 9
    IO = 10
    CRC_MISMATCH = 11
    OFFSET_MISMATCH = 12
    LIMIT_EXCEEDED = 13
    NOT_RUNNING = 14
    TIMEOUT = 15
    INTERNAL = 16
    CONFIRMATION_REQUIRED = 17
    SESSION_EXPIRED = 18


class CodecStatus(IntEnum):
    OK = 0
    INCOMPLETE = 1
    FRAME_READY = 2

    ERROR_ARGUMENT = 16
    ERROR_OUTPUT_TOO_SMALL = 17
    ERROR_COBS_MALFORMED = 18
    ERROR_ENCODED_TOO_LARGE = 19
    ERROR_FRAME_TOO_SHORT = 20
    ERROR_BAD_MAGIC = 21
    ERROR_UNSUPPORTED_VERSION = 22
    ERROR_UNKNOWN_TYPE = 23
    ERROR_RESERVED_FLAGS = 24
    ERROR_INVALID_ENVELOPE = 25
    ERROR_PAYLOAD_TOO_LARGE = 26
    ERROR_LENGTH_MISMATCH = 27
    ERROR_CRC_MISMATCH = 28


class ProtocolDecodeError(ValueError):
    """A rejected packet with a stable status shared with the C codec."""

    def __init__(self, status: CodecStatus, message: str | None = None):
        self.status = status
        super().__init__(message or status.name.lower().replace("_", "-"))


@dataclass(frozen=True, slots=True)
class Frame:
    type: MessageType
    flags: FrameFlag = FrameFlag(0)
    request_id: int = 0
    payload: bytes = b""


@dataclass(frozen=True, slots=True)
class StreamEvent:
    """Exactly one of ``frame`` and ``error`` is present."""

    frame: Frame | None = None
    error: ProtocolDecodeError | None = None

    @property
    def ok(self) -> bool:
        return self.frame is not None


def _fail(status: CodecStatus, message: str | None = None) -> None:
    raise ProtocolDecodeError(status, message)


def _coerce_type(value: MessageType | int) -> MessageType:
    try:
        return MessageType(value)
    except (TypeError, ValueError):
        _fail(CodecStatus.ERROR_UNKNOWN_TYPE)


def _coerce_flags(value: FrameFlag | int) -> FrameFlag:
    if not isinstance(value, int):
        _fail(CodecStatus.ERROR_ARGUMENT)
    if value < 0 or value > 0xFFFF:
        _fail(CodecStatus.ERROR_RESERVED_FLAGS)
    if value & ~int(FRAME_FLAG_MASK):
        _fail(CodecStatus.ERROR_RESERVED_FLAGS)
    return FrameFlag(value)


def _validate_envelope(
    message_type: MessageType | int,
    flags: FrameFlag | int,
    request_id: int,
) -> tuple[MessageType, FrameFlag]:
    parsed_type = _coerce_type(message_type)
    parsed_flags = _coerce_flags(flags)
    if not isinstance(request_id, int) or request_id < 0 or request_id > 0xFFFFFFFF:
        _fail(CodecStatus.ERROR_INVALID_ENVELOPE)

    is_event = MessageType.STDOUT <= parsed_type <= MessageType.DROPPED
    if is_event:
        if parsed_flags or request_id != 0:
            _fail(CodecStatus.ERROR_INVALID_ENVELOPE)
        return parsed_type, parsed_flags

    if request_id == 0:
        _fail(CodecStatus.ERROR_INVALID_ENVELOPE)
    if parsed_flags & FrameFlag.ERROR and not parsed_flags & FrameFlag.RESPONSE:
        _fail(CodecStatus.ERROR_INVALID_ENVELOPE)
    if parsed_flags & FrameFlag.MORE and not parsed_flags & FrameFlag.RESPONSE:
        _fail(CodecStatus.ERROR_INVALID_ENVELOPE)
    return parsed_type, parsed_flags


def crc32(data: bytes | bytearray | memoryview) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


def cobs_encode(data: bytes | bytearray | memoryview) -> bytes:
    """Encode bytes with the HMPY COBS implementation."""

    source = bytes(data)
    output = bytearray(b"\x00")
    code_index = 0
    code = 1

    for value in source:
        if value == 0:
            output[code_index] = code
            code_index = len(output)
            output.append(0)
            code = 1
        else:
            output.append(value)
            code += 1
            if code == 0xFF:
                output[code_index] = code
                code_index = len(output)
                output.append(0)
                code = 1
    output[code_index] = code
    return bytes(output)


def cobs_decode(packet: bytes | bytearray | memoryview) -> bytes:
    """Decode a COBS packet that does not contain the zero delimiter."""

    source = bytes(packet)
    if not source:
        _fail(CodecStatus.ERROR_COBS_MALFORMED)
    if 0 in source:
        _fail(CodecStatus.ERROR_COBS_MALFORMED)

    output = bytearray()
    read_index = 0
    while read_index < len(source):
        code = source[read_index]
        read_index += 1
        if code == 0:
            _fail(CodecStatus.ERROR_COBS_MALFORMED)
        copy_length = code - 1
        if copy_length > len(source) - read_index:
            _fail(CodecStatus.ERROR_COBS_MALFORMED)
        output.extend(source[read_index : read_index + copy_length])
        read_index += copy_length
        if code != 0xFF and read_index < len(source):
            output.append(0)
        if len(output) > MAX_RAW_FRAME:
            _fail(CodecStatus.ERROR_ENCODED_TOO_LARGE)
    return bytes(output)


def encode_raw(frame: Frame) -> bytes:
    message_type, flags = _validate_envelope(frame.type, frame.flags, frame.request_id)
    try:
        payload = bytes(frame.payload)
    except (TypeError, ValueError):
        _fail(CodecStatus.ERROR_ARGUMENT)
    if len(payload) > MAX_PAYLOAD_SIZE:
        _fail(CodecStatus.ERROR_PAYLOAD_TOO_LARGE)

    raw_without_crc = HEADER.pack(
        MAGIC,
        PROTOCOL_VERSION,
        int(message_type),
        int(flags),
        frame.request_id,
        len(payload),
    ) + payload
    return raw_without_crc + CRC.pack(crc32(raw_without_crc))


def encode_frame(frame: Frame) -> bytes:
    """Encode one frame, including the final ``0x00`` wire delimiter."""

    encoded = cobs_encode(encode_raw(frame))
    if len(encoded) > MAX_ENCODED_PACKET:
        _fail(CodecStatus.ERROR_ENCODED_TOO_LARGE)
    return encoded + b"\x00"


def decode_raw(raw: bytes | bytearray | memoryview) -> Frame:
    source = bytes(raw)
    if len(source) < RAW_OVERHEAD:
        _fail(CodecStatus.ERROR_FRAME_TOO_SHORT)
    if source[:4] != MAGIC:
        _fail(CodecStatus.ERROR_BAD_MAGIC)
    if source[4] != PROTOCOL_VERSION:
        _fail(CodecStatus.ERROR_UNSUPPORTED_VERSION)

    magic, version, message_type, flags, request_id, payload_length = HEADER.unpack_from(source)
    del magic, version
    if payload_length > MAX_PAYLOAD_SIZE:
        _fail(CodecStatus.ERROR_PAYLOAD_TOO_LARGE)
    expected_length = RAW_OVERHEAD + payload_length
    if len(source) != expected_length:
        _fail(CodecStatus.ERROR_LENGTH_MISMATCH)

    parsed_type, parsed_flags = _validate_envelope(message_type, flags, request_id)
    crc_offset = HEADER_SIZE + payload_length
    expected_crc = CRC.unpack_from(source, crc_offset)[0]
    if crc32(source[:crc_offset]) != expected_crc:
        _fail(CodecStatus.ERROR_CRC_MISMATCH)
    return Frame(parsed_type, parsed_flags, request_id, source[HEADER_SIZE:crc_offset])


def decode_packet(packet: bytes | bytearray | memoryview) -> Frame:
    """Decode one COBS packet without its trailing delimiter."""

    source = bytes(packet)
    if len(source) > MAX_ENCODED_PACKET:
        _fail(CodecStatus.ERROR_ENCODED_TOO_LARGE)
    return decode_raw(cobs_decode(source))


def decode_frame(wire_frame: bytes | bytearray | memoryview) -> Frame:
    """Decode exactly one wire frame, requiring one trailing delimiter."""

    source = bytes(wire_frame)
    if not source or source[-1] != 0 or 0 in source[:-1]:
        _fail(CodecStatus.ERROR_COBS_MALFORMED)
    return decode_packet(source[:-1])


class StreamDecoder:
    """Delimiter-resynchronising streaming decoder with bounded storage."""

    __slots__ = ("_buffer", "_discarding", "error_count", "last_error")

    def __init__(self) -> None:
        self._buffer = bytearray()
        self._discarding = False
        self.error_count = 0
        self.last_error: CodecStatus | None = None

    def reset(self) -> None:
        self._buffer.clear()
        self._discarding = False
        self.last_error = None

    def _error_event(self, error: ProtocolDecodeError) -> StreamEvent:
        self.error_count += 1
        self.last_error = error.status
        return StreamEvent(error=error)

    def feed(self, data: bytes | bytearray | memoryview | Iterable[int]) -> list[StreamEvent]:
        events: list[StreamEvent] = []
        try:
            values = bytes(data)
        except (TypeError, ValueError) as exc:
            raise ProtocolDecodeError(CodecStatus.ERROR_ARGUMENT) from exc

        for value in values:
            if value != 0:
                if self._discarding:
                    continue
                if len(self._buffer) >= MAX_ENCODED_PACKET:
                    self._buffer.clear()
                    self._discarding = True
                    continue
                self._buffer.append(value)
                continue

            if self._discarding:
                self._discarding = False
                self._buffer.clear()
                events.append(
                    self._error_event(
                        ProtocolDecodeError(CodecStatus.ERROR_ENCODED_TOO_LARGE)
                    )
                )
                continue
            if not self._buffer:
                continue

            packet = bytes(self._buffer)
            self._buffer.clear()
            try:
                frame = decode_packet(packet)
            except ProtocolDecodeError as error:
                events.append(self._error_event(error))
            else:
                self.last_error = None
                events.append(StreamEvent(frame=frame))
        return events

    def feed_frames(
        self, data: bytes | bytearray | memoryview | Iterable[int]
    ) -> list[Frame]:
        """Convenience wrapper that returns valid frames and counts errors."""

        return [event.frame for event in self.feed(data) if event.frame is not None]


__all__ = [
    "CodecStatus",
    "ErrorCode",
    "Frame",
    "FrameFlag",
    "LINE_HANDSHAKE",
    "LINE_READY",
    "MAGIC",
    "MAX_ENCODED_PACKET",
    "MAX_PAYLOAD_SIZE",
    "MAX_RAW_FRAME",
    "MAX_WIRE_FRAME",
    "MessageType",
    "PROTOCOL_VERSION",
    "ProtocolDecodeError",
    "StreamDecoder",
    "StreamEvent",
    "cobs_decode",
    "cobs_encode",
    "crc32",
    "decode_frame",
    "decode_packet",
    "decode_raw",
    "encode_frame",
    "encode_raw",
]
