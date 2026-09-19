"""Decoded payload types returned by the SEN0305."""

from __future__ import annotations

import struct
from dataclasses import dataclass

#: Every RETURN_INFO, RETURN_BLOCK and RETURN_ARROW payload is five
#: little-endian uint16 values.
_PAYLOAD = struct.Struct("<HHHHH")
PAYLOAD_LENGTH = _PAYLOAD.size


class PayloadError(ValueError):
    """Raised when a payload is not the length the protocol requires."""


def _unpack(data: bytes, kind: str) -> tuple[int, int, int, int, int]:
    if len(data) != PAYLOAD_LENGTH:
        raise PayloadError(
            f"{kind} payload is {len(data)} bytes, expected {PAYLOAD_LENGTH}"
        )
    return _PAYLOAD.unpack(data)


@dataclass(frozen=True)
class Info:
    """RETURN_INFO (0x29): the header that precedes a batch of results."""

    result_count: int
    learned_count: int
    frame_number: int

    @classmethod
    def decode(cls, data: bytes) -> "Info":
        count, learned, frame, _reserved0, _reserved1 = _unpack(data, "RETURN_INFO")
        return cls(result_count=count, learned_count=learned, frame_number=frame)


@dataclass(frozen=True)
class Block:
    """RETURN_BLOCK (0x2A): an axis-aligned detection box.

    Coordinates are in the fixed 320x240 screen space. ``object_id`` of 0 means
    the object was detected but never learned.
    """

    x_center: int
    y_center: int
    width: int
    height: int
    object_id: int

    @classmethod
    def decode(cls, data: bytes) -> "Block":
        x, y, w, h, oid = _unpack(data, "RETURN_BLOCK")
        return cls(x_center=x, y_center=y, width=w, height=h, object_id=oid)

    @property
    def is_learned(self) -> bool:
        return self.object_id != 0

    @property
    def corners(self) -> tuple[int, int, int, int]:
        """Return ``(x_min, y_min, x_max, y_max)``."""
        return (
            self.x_center - self.width // 2,
            self.y_center - self.height // 2,
            self.x_center + self.width // 2,
            self.y_center + self.height // 2,
        )


@dataclass(frozen=True)
class Arrow:
    """RETURN_ARROW (0x2B): a directed segment, used by line tracking."""

    x_origin: int
    y_origin: int
    x_target: int
    y_target: int
    object_id: int

    @classmethod
    def decode(cls, data: bytes) -> "Arrow":
        xo, yo, xt, yt, oid = _unpack(data, "RETURN_ARROW")
        return cls(
            x_origin=xo, y_origin=yo, x_target=xt, y_target=yt, object_id=oid
        )

    @property
    def is_learned(self) -> bool:
        return self.object_id != 0


@dataclass(frozen=True)
class Results:
    """A complete response batch: one Info plus its blocks and arrows."""

    info: Info
    blocks: tuple[Block, ...] = ()
    arrows: tuple[Arrow, ...] = ()

    def __len__(self) -> int:
        return len(self.blocks) + len(self.arrows)
