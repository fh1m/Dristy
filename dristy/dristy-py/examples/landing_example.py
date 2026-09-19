#!/usr/bin/env python3
"""
Dristy Precision Landing Example
==================================

Uses AprilTag or ArUco marker detection with 6-DOF pose for precision landing.
Compatible with ArduPilot's precision landing protocol.

The drone descends toward a fiducial marker, using Dristy's PnP pose estimate
for 3D position. When close enough, transitions to visual servoing for the
final approach.

Phases:
  1. SEARCH:  Spin/orbit looking for the landing pad tag
  2. APPROACH: Descend toward tag using 3D pose
  3. FINAL:   Below 1m, switch to 2D visual servoing
  4. LAND:    Below 0.3m, trigger landing
"""

import time
from enum import IntEnum
from dristy import Dristy, Mode


class LandingPhase(IntEnum):
    SEARCH = 0
    APPROACH = 1
    FINAL = 2
    LAND = 3


def main():
    cam = Dristy("/dev/ttyUSB0", 115200)
    cam.connect()
    cam.set_mode(Mode.LANDING_TARGET)
    cam.lcd_off()

    # Configure for the landing pad marker
    # cam.filter_class(0)  # if using detection mode
    # For AprilTag/ArUco mode, the marker ID is the target

    phase = LandingPhase.SEARCH
    print("Precision landing started. Looking for marker...")

    for frame in cam.stream(rate_hz=20):
        t = frame.target

        if phase == LandingPhase.SEARCH:
            if t.valid and t.type.value == 3:  # TAG target
                print(f"Marker found! Range: {t.range_mm} mm")
                phase = LandingPhase.APPROACH

        elif phase == LandingPhase.APPROACH:
            if not t.valid:
                phase = LandingPhase.SEARCH
                print("Lost marker, searching...")
                continue

            print(f"APPROACH: range={t.range_mm}mm "
                  f"error=({t.error_x}, {t.error_y}) "
                  f"conf={t.confidence}%")

            # Descend while centering
            if t.range_mm > 0 and t.range_mm < 1000:
                phase = LandingPhase.FINAL
                print("Entering FINAL approach")

        elif phase == LandingPhase.FINAL:
            if not t.valid:
                phase = LandingPhase.SEARCH
                continue

            print(f"FINAL: error=({t.error_x}, {t.error_y}) "
                  f"range={t.range_mm}mm centred={t.centred}")

            if t.range_mm > 0 and t.range_mm < 300 and t.centred:
                phase = LandingPhase.LAND
                print("LAND command!")

        elif phase == LandingPhase.LAND:
            print("Landing complete.")
            break

    cam.close()


if __name__ == "__main__":
    main()
