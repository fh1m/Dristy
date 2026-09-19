"""Frame encoding and decoding for the HUSKYLENS 0x55 0xAA protocol.

Frame layout, from the v0.5.1 protocol document:

    0x55 | 0xAA | address | data_length | command | data[data_length] | checksum

The checksum is the low byte of the arithmetic sum of every preceding byte.
There is no escaping and no end delimiter, so a desynchronised reader has to
recover by scanning for the next plausible preamble.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from typing import Iterator

HEADER = b"\x55\xAA"
DEFAULT_ADDRESS = 0x11

#: Preamble (2) + address + length + command + checksum.
FRAME_OVERHEAD = 6
MAX_DATA_LENGTH = 0xFF


class FrameError(ValueError):
    """Raised when bytes cannot be interpreted as a valid frame."""


@dataclass(frozen=True)
class Frame:
    command: int
    data: bytes = b""
    address: int = DEFAULT_ADDRESS

    def encode(self) -> bytes:
        if len(self.data) > MAX_DATA_LENGTH:
            raise FrameError(
                f"data length {len(self.data)} exceeds the single-byte length field"
            )
        body = HEADER + bytes([self.address, len(self.data), self.command]) + self.data
        return body + bytes([sum(body) & 0xFF])


def encode(command: int, data: bytes = b"", address: int = DEFAULT_ADDRESS) -> bytes:
    return Frame(command=command, data=data, address=address).encode()


def encode_uint16(value: int) -> bytes:
    """Encode a protocol uint16 argument (little-endian, as used by IDs)."""
    return struct.pack("<H", value)


def iter_frames(buffer: bytes) -> Iterator[tuple[Frame, int, int]]:
    """Yield every valid frame found in ``buffer``.

    Yields ``(frame, start_offset, end_offset)``. Bytes that do not parse are
    skipped one at a time, so a burst of line noise costs at most a resync
    rather than dropping the frames that follow it.
    """
    index = 0
    limit = len(buffer)
    while index + FRAME_OVERHEAD <= limit:
        start = buffer.find(HEADER, index)
        if start < 0 or start + FRAME_OVERHEAD > limit:
            return
        length = buffer[start + 3]
        end = start + FRAME_OVERHEAD + length
        if end > limit:
            # Frame is real but incomplete; wait for more bytes.
            return
        body = buffer[start : end - 1]
        if buffer[end - 1] != (sum(body) & 0xFF):
            index = start + 1
            continue
        yield (
            Frame(
                command=buffer[start + 4],
                data=bytes(buffer[start + 5 : end - 1]),
                address=buffer[start + 2],
            ),
            start,
            end,
        )
        index = end


def decode_one(buffer: bytes) -> tuple[Frame, int]:
    """Decode the first valid frame, returning it and the offset just past it."""
    for frame, _start, end in iter_frames(buffer):
        return frame, end
    raise FrameError("no complete, checksum-valid frame in buffer")
