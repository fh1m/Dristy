"""Dristy data types — all results from the vision pipeline."""

from dataclasses import dataclass, field
from enum import IntEnum
from typing import List, Optional, Tuple


class Mode(IntEnum):
    """Vision modes (must match dristy_modes.h)."""
    DETECT          = 0x00
    DETECT_CUSTOM   = 0x01
    CLASSIFY        = 0x02
    FACE_DETECT     = 0x03
    FACE_RECOGNISE  = 0x04
    COLOUR_TRACK    = 0x10
    LINE_FOLLOW     = 0x11
    APRILTAG        = 0x12
    QR_CODE         = 0x13
    OPTICAL_FLOW    = 0x14
    COLOUR_SORT     = 0x15
    ARUCO           = 0x16
    MOTION_DETECT   = 0x17
    DETECT_TRACK    = 0x20
    DETECT_TAG      = 0x21
    LANDING_TARGET  = 0x22
    DETECT_ARUCO    = 0x23
    DETECT_MOTION   = 0x24

    @classmethod
    def from_name(cls, name: str) -> "Mode":
        """Look up mode by name string (case-insensitive)."""
        name = name.upper().replace("-", "_").replace(" ", "_")
        for m in cls:
            if m.name == name:
                return m
        raise ValueError(f"Unknown mode: {name}. Valid: {[m.name for m in cls]}")


class CameraPreset(IntEnum):
    """Camera ISP presets."""
    AUTO       = 0
    INDOOR     = 1
    OUTDOOR    = 2
    NIGHT      = 3
    FAST       = 4
    TAG_DETECT = 5
    CUSTOM     = 6


class TargetType(IntEnum):
    """Primary target types."""
    NONE      = 0
    DETECTION = 1
    TRACK     = 2
    TAG       = 3
    BLOB      = 4
    LINE      = 5


@dataclass
class Target:
    """Primary target — the single most important object for control loops.

    Coordinates are NORMALISED: [-1000, +1000], centre = 0.
    Directly usable as PID error signals:
        error_x > 0 → target is RIGHT of centre → yaw right
        error_y > 0 → target is BELOW centre → pitch down
        size > 0    → target is visible, larger = closer
    """
    type: TargetType = TargetType.NONE
    error_x: int = 0           # [-1000, +1000] → yaw PID input
    error_y: int = 0           # [-1000, +1000] → pitch PID input
    size: int = 0              # [0, 2000] → approach PID input
    error_rate_x: int = 0      # d(error_x)/dt in norm units/second
    error_rate_y: int = 0      # d(error_y)/dt
    range_mm: int = 0          # 3D range (0 = unknown)
    track_id: int = 0
    confidence: int = 0        # 0-100%
    timestamp_ms: int = 0

    @property
    def valid(self) -> bool:
        return self.type != TargetType.NONE

    @property
    def centred(self) -> bool:
        """True if target is within 10% of frame centre."""
        return abs(self.error_x) < 100 and abs(self.error_y) < 100


@dataclass
class Track:
    """Tracked object with velocity estimate."""
    id: int = 0
    cx: int = 0                # normalised centre X [-1000, +1000]
    cy: int = 0                # normalised centre Y
    vel_x: int = 0             # normalised velocity X (units/second)
    vel_y: int = 0
    cls: int = 0               # class ID
    confidence: int = 0        # 0-100%


@dataclass
class Tag:
    """Fiducial marker (AprilTag or ArUco) with optional 6-DOF pose."""
    tag_id: int = 0
    family: int = 0            # 0=TAG36H11, 1=TAG25H9, etc.
    cx: int = 0                # normalised centre
    cy: int = 0
    range_mm: int = 0          # 3D distance (0 = no pose)
    yaw_deg: float = 0.0
    pitch_deg: float = 0.0
    hamming: int = 0

    @property
    def has_pose(self) -> bool:
        return self.range_mm > 0


@dataclass
class Blob:
    """Colour blob detection result."""
    cx: int = 0
    cy: int = 0
    w: int = 0
    h: int = 0
    colour_id: int = 0
    density: float = 0.0       # 0..1 (1 = solid)


@dataclass
class FlowCell:
    """Optical flow measurement at a point."""
    roi_cx: int = 0
    roi_cy: int = 0
    dx: float = 0.0            # sub-pixel displacement X
    dy: float = 0.0            # sub-pixel displacement Y


@dataclass
class MotionRegion:
    """Detected motion region."""
    x: int = 0
    y: int = 0
    w: int = 0
    h: int = 0
    pixel_count: int = 0
    intensity: float = 0.0


@dataclass
class Frame:
    """Complete vision result from one pipeline tick.

    This is the primary data structure you work with.
    Every call to dristy.read() or iteration of dristy.stream() yields a Frame.
    """
    timestamp_ms: int = 0
    frame_number: int = 0
    mode: Mode = Mode.DETECT_TRACK
    fps: float = 0.0
    pipeline_us: int = 0
    inference_us: int = 0

    target: Target = field(default_factory=Target)
    tracks: List[Track] = field(default_factory=list)
    tags: List[Tag] = field(default_factory=list)
    blobs: List[Blob] = field(default_factory=list)
    flow: List[FlowCell] = field(default_factory=list)
    motion_regions: List[MotionRegion] = field(default_factory=list)
    motion_percent: float = 0.0

    @property
    def has_target(self) -> bool:
        return self.target.valid

    @property
    def detection_count(self) -> int:
        return len(self.tracks)

    @property
    def tag_count(self) -> int:
        return len(self.tags)


@dataclass
class PerfReport:
    """Benchmark / performance report."""
    frames: int = 0
    fps: float = 0.0
    kpu_util: float = 0.0      # 0-100%
    stages: dict = field(default_factory=dict)
