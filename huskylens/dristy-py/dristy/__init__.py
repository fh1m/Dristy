"""
dristy — Python API for the Dristy Vision Co-Processor
=======================================================

Full control over the Dristy firmware running on HuskyLens (K210).
Zero dependencies beyond pyserial. Works on any platform.

Quick start:
    from dristy import Dristy

    cam = Dristy("/dev/ttyUSB0")
    cam.set_mode("detect_track")
    cam.lcd_off()  # headless for robotics

    for frame in cam.stream():
        if frame.target:
            print(f"Target at ({frame.target.error_x}, {frame.target.error_y})")
            print(f"Range: {frame.target.range_mm} mm")
"""

from dristy.core import Dristy, DristyError, DristyTimeout
from dristy.types import (
    Target, Track, Tag, Blob, FlowCell, MotionRegion,
    Frame, PerfReport, Mode, CameraPreset,
)

__version__ = "0.1.0"
__all__ = [
    "Dristy", "DristyError", "DristyTimeout",
    "Target", "Track", "Tag", "Blob", "FlowCell", "MotionRegion",
    "Frame", "PerfReport", "Mode", "CameraPreset",
]
