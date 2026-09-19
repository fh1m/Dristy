#!/usr/bin/env python3
"""
Dristy Drone PID Example
==========================

Demonstrates using Dristy as a vision co-processor for drone control.
Reads target position and feeds it into PID controllers for yaw/pitch.

This runs on the companion computer (Pi, Jetson, etc.) connected to
both Dristy (UART) and the flight controller (MAVLink/UART).

Usage:
    python3 drone_pid_example.py --port /dev/ttyAMA0 --baud 921600
"""

import argparse
import time
from dristy import Dristy, Mode


class SimplePID:
    """Minimal PID controller."""
    def __init__(self, kp=1.0, ki=0.0, kd=0.0, limit=500):
        self.kp, self.ki, self.kd = kp, ki, kd
        self.limit = limit
        self.integral = 0.0
        self.prev_error = 0.0
        self.prev_time = time.monotonic()

    def update(self, error: float) -> float:
        now = time.monotonic()
        dt = now - self.prev_time
        if dt < 0.001:
            return 0.0

        self.integral += error * dt
        self.integral = max(-self.limit, min(self.limit, self.integral))
        derivative = (error - self.prev_error) / dt
        output = self.kp * error + self.ki * self.integral + self.kd * derivative

        self.prev_error = error
        self.prev_time = now
        return max(-self.limit, min(self.limit, output))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/dev/ttyUSB0")
    parser.add_argument("--baud", default=115200, type=int)
    parser.add_argument("--mode", default="detect_track")
    args = parser.parse_args()

    # Connect to Dristy
    cam = Dristy(args.port, args.baud)
    ident = cam.connect()
    print(f"Connected: {ident}")

    # Configure for drone use
    cam.set_mode(args.mode)
    cam.lcd_off()  # headless mode — saves 8 ms/frame

    # PID controllers
    # Error is in normalised units [-1000, +1000]
    # Output is in arbitrary units for your mixer
    pid_yaw = SimplePID(kp=0.5, ki=0.01, kd=0.1)
    pid_pitch = SimplePID(kp=0.5, ki=0.01, kd=0.1)
    pid_throttle = SimplePID(kp=0.3, ki=0.005, kd=0.05)

    print("Reading vision stream...")
    print(f"{'Time':>8s} {'ErrX':>6s} {'ErrY':>6s} {'Size':>5s} {'Range':>6s} "
          f"{'Yaw':>6s} {'Pitch':>6s} {'Throt':>6s} {'FPS':>5s}")
    print("-" * 70)

    target_size = 800  # desired target size for approach control

    for frame in cam.stream(rate_hz=20):
        if not frame.has_target:
            # No target — hold position (zero PID inputs)
            pid_yaw.update(0)
            pid_pitch.update(0)
            print(f"{time.monotonic():8.1f}   --- no target ---")
            continue

        t = frame.target

        # PID inputs (error signals from Dristy are already normalised!)
        yaw_cmd = pid_yaw.update(t.error_x)
        pitch_cmd = pid_pitch.update(t.error_y)

        # Approach: positive error when target is too small (far away)
        size_error = target_size - t.size
        throttle_cmd = pid_throttle.update(size_error)

        # If we have 3D range from AprilTag pose, use that for altitude
        if t.range_mm > 0:
            alt_error = t.range_mm - 1000  # hold at 1m
            throttle_cmd = pid_throttle.update(alt_error)

        print(f"{time.monotonic():8.1f} {t.error_x:6d} {t.error_y:6d} "
              f"{t.size:5d} {t.range_mm:6d} "
              f"{yaw_cmd:6.1f} {pitch_cmd:6.1f} {throttle_cmd:6.1f} "
              f"{frame.fps:5.1f}")

        # TODO: Send yaw_cmd, pitch_cmd, throttle_cmd to flight controller
        # via MAVLink SET_ATTITUDE_TARGET or RC_CHANNELS_OVERRIDE


if __name__ == "__main__":
    main()
