"""An open, typed driver for the DFRobot HUSKYLENS 0x55 0xAA protocol."""

from .client import (
    DeviceBusy,
    HuskyLens,
    MutationNotAllowed,
    ProFeatureRequired,
    ProtocolError,
)
from .commands import MUTATING_COMMANDS, Algorithm, Command
from .frames import Frame, FrameError, encode, iter_frames
from .transport import CANDIDATE_BAUDS, SerialTransport, TransportError, find_ports
from .types import Arrow, Block, Info, Results

__version__ = "0.1.0"

__all__ = [
    "Algorithm",
    "Arrow",
    "Block",
    "CANDIDATE_BAUDS",
    "Command",
    "DeviceBusy",
    "Frame",
    "FrameError",
    "HuskyLens",
    "Info",
    "MUTATING_COMMANDS",
    "MutationNotAllowed",
    "ProFeatureRequired",
    "ProtocolError",
    "Results",
    "SerialTransport",
    "TransportError",
    "encode",
    "find_ports",
    "iter_frames",
]
