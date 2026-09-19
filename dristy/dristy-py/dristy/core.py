"""
Dristy — Core API class.

Provides full control over a Dristy device via UART.
Thread-safe: one reader thread handles incoming data, main thread sends commands.
"""

import struct
import threading
import time
from typing import Generator, Optional

import serial

from dristy.protocol import (
    build_packet, parse_packet, parse_ctrl_packet,
    CMD_IDENTIFY, CMD_SET_MODE, CMD_GET_MODE, CMD_GET_TRACKS, CMD_GET_TAGS,
    CMD_GET_QR, CMD_GET_FLOW, CMD_GET_BLOBS, CMD_GET_PERF, CMD_SET_THRESHOLD, CMD_SET_TARGET_ID,
    CMD_SET_TARGET_CLS, CMD_CTRL_TARGET, CMD_CTRL_FULL, CMD_GET_RESULT,
    CMD_BENCH_ENABLE, CMD_BENCH_REPORT, CMD_BENCH_RESET, CMD_BENCH_PRINT,
    CMD_KNOCK, CMD_RETURN_OK, CMD_SET_BAUD,
    CMD_LCD_CONTROL, CMD_LCD_BRIGHTNESS,
    CMD_CAM_PRESET, CMD_CAM_SET_PARAM, CMD_CAM_GET_PARAMS,
    CMD_LED_MODE, CMD_LED_BRIGHTNESS, CMD_LED_COLOR, CMD_LED_ILLUMINATION,
    CMD_CONFIG_SAVE, CMD_CONFIG_LOAD, CMD_CONFIG_RESET,
    CMD_ARUCO_SET_DICT, CMD_ARUCO_SET_SIZE, CMD_MOTION_SENSITIVITY,
    RET_TRACK, RET_TAG, RET_BLOB, RET_PERF, RET_MODE, RET_IDENTITY,
    RET_QR, RET_FLOW,
    RET_CAM_PARAMS, RET_LCD_STATE,
    CTRL_HEARTBEAT, CTRL_TARGET, CTRL_FULL_FRAME,
)
from dristy.types import (
    Mode, CameraPreset, TargetType,
    Target, Track, Tag, Blob, FlowCell,
    Frame, PerfReport,
)


class DristyError(Exception):
    """Base Dristy exception."""
    pass


class DristyTimeout(DristyError):
    """Command timed out."""
    pass


class Dristy:
    """Full-control API for a Dristy vision co-processor.

    Usage:
        cam = Dristy("/dev/ttyUSB0")       # USB debug port
        cam = Dristy("/dev/ttyAMA0", 921600)  # Gravity UART on Pi

        cam.set_mode(Mode.DETECT_TRACK)
        cam.lcd_off()

        # Single read
        frame = cam.read()
        print(frame.target)

        # Continuous stream
        for frame in cam.stream():
            if frame.target.valid:
                yaw_error = frame.target.error_x
                pitch_error = frame.target.error_y

        cam.close()

    Context manager:
        with Dristy("/dev/ttyUSB0") as cam:
            cam.set_mode("detect_track")
            frame = cam.read()
    """

    def __init__(self, port: str, baud: int = 115200, timeout: float = 1.0):
        self._port = port
        self._baud = baud
        self._timeout = timeout
        self._ser = serial.Serial(port, baud, timeout=timeout)
        self._ser.dtr = False
        self._ser.rts = False
        self._lock = threading.Lock()
        self._rx_buf = bytearray()
        self._last_frame = Frame()
        self._connected = False
        self._identity = ""

    def __enter__(self):
        self.connect()
        return self

    def __exit__(self, *args):
        self.close()

    def close(self):
        """Close the serial connection."""
        if self._ser and self._ser.is_open:
            self._ser.close()

    # ── Connection ──────────────────────────────────────────────────────

    def connect(self, retries: int = 3) -> str:
        """Connect and identify the device. Returns identity string."""
        for attempt in range(retries):
            try:
                # Try DLP identify first
                resp = self._command(CMD_IDENTIFY)
                if resp:
                    self._identity = resp.decode("utf-8", errors="replace").rstrip("\x00")
                    self._connected = True
                    return self._identity

                # Fall back to stock Dristy knock
                resp = self._command(CMD_KNOCK)
                if resp is not None:
                    self._identity = "Dristy (stock)"
                    self._connected = True
                    return self._identity

            except (DristyTimeout, serial.SerialException):
                if attempt < retries - 1:
                    time.sleep(0.5)
                    continue

        raise DristyError(f"Cannot connect to {self._port} after {retries} attempts")

    @property
    def connected(self) -> bool:
        return self._connected

    @property
    def identity(self) -> str:
        return self._identity

    # ── Mode control ────────────────────────────────────────────────────

    def set_mode(self, mode) -> None:
        """Switch vision mode.

        Args:
            mode: Mode enum, int, or string name (e.g., "detect_track")
        """
        if isinstance(mode, str):
            mode = Mode.from_name(mode)
        elif isinstance(mode, int) and not isinstance(mode, Mode):
            mode = Mode(mode)
        self._command(CMD_SET_MODE, bytes([int(mode)]))

    def get_mode(self) -> Mode:
        """Get current vision mode."""
        resp = self._command(CMD_GET_MODE)
        if resp and len(resp) >= 1:
            try:
                return Mode(resp[0])
            except ValueError:
                return Mode.DETECT_TRACK
        return Mode.DETECT_TRACK

    # ── LCD control ─────────────────────────────────────────────────────

    def lcd_off(self) -> None:
        """Turn off the LCD (headless / drone mode)."""
        self._command(CMD_LCD_CONTROL, bytes([0]))

    def lcd_on(self, overlay: bool = True) -> None:
        """Turn on the LCD; overlay=True shows debug boxes."""
        self._command(CMD_LCD_CONTROL, bytes([1 if overlay else 2]))

    def lcd_brightness(self, percent: int) -> None:
        """Set backlight 0–100."""
        self._command(CMD_LCD_BRIGHTNESS, bytes([max(0, min(100, percent))]))

    # ── Reading results ─────────────────────────────────────────────────

    def read(self) -> Frame:
        """Read the latest vision frame from the device.

        Returns a Frame with target, tracks, tags, etc.
        Blocks until a response is received or timeout.
        """
        resp = self._command(CMD_GET_RESULT)
        if not resp or len(resp) < 16:
            return Frame()

        return self._parse_result_response(resp)

    def read_target(self) -> Target:
        """Read just the primary target (fastest — 20 bytes)."""
        resp = self._command(CMD_CTRL_TARGET)
        if not resp or len(resp) < 4:
            return Target()
        return self._parse_ctrl_target(resp)

    def read_tracks(self) -> list:
        """Read all tracked objects."""
        resp = self._command(CMD_GET_TRACKS)
        if not resp:
            return []
        return self._parse_tracks(resp)

    def read_tags(self) -> list:
        """Read all fiducial tag detections."""
        resp = self._command(CMD_GET_TAGS)
        if not resp:
            return []
        return self._parse_tags(resp)

    def read_blobs(self) -> list:
        """Read colour blob detections."""
        resp = self._command(CMD_GET_BLOBS)
        if not resp:
            return []
        return self._parse_blobs(resp)

    def read_qr(self) -> list:
        """Read decoded QR strings (length-prefixed chunks from device)."""
        resp = self._command(CMD_GET_QR)
        if not resp:
            return []
        return self._parse_qr(resp)

    def read_flow(self) -> list:
        """Read optical flow cells (may be empty until flow mode is active)."""
        resp = self._command(CMD_GET_FLOW)
        if not resp:
            return []
        return self._parse_flow(resp)

    def stream(self, rate_hz: float = 30.0) -> Generator[Frame, None, None]:
        """Yield Frames continuously at the given rate.

        Usage:
            for frame in cam.stream(rate_hz=20):
                if frame.target.valid:
                    do_something(frame.target)
        """
        interval = 1.0 / rate_hz
        while True:
            t0 = time.monotonic()
            try:
                yield self.read()
            except DristyTimeout:
                yield Frame()  # yield empty frame on timeout
            elapsed = time.monotonic() - t0
            if elapsed < interval:
                time.sleep(interval - elapsed)

    # ── Target selection ────────────────────────────────────────────────

    def lock_target(self, track_id: int) -> None:
        """Lock the primary target to a specific track ID.

        The control output will always report this track as the primary target,
        regardless of position or confidence.
        """
        self._command(CMD_SET_TARGET_ID,
                      struct.pack("<H", track_id))

    def unlock_target(self) -> None:
        """Release target lock (auto-select best target)."""
        self.lock_target(0)

    def filter_class(self, class_id: int) -> None:
        """Filter primary target by class. 255 = any class."""
        self._command(CMD_SET_TARGET_CLS, bytes([class_id & 0xFF]))

    # ── Configuration ───────────────────────────────────────────────────

    def set_confidence(self, percent: int) -> None:
        """Set detection confidence threshold (0-100%)."""
        thresh = max(0, min(100, percent)) * 100
        self._command(CMD_SET_THRESHOLD, struct.pack("<H", thresh))

    def set_baud(self, baud: int) -> None:
        """Change Gravity UART baud rate.

        WARNING: This changes the device baud rate. You must reconnect
        at the new baud rate after calling this.
        """
        self._command(CMD_SET_BAUD, struct.pack("<I", baud))

    def camera_preset(self, preset: int) -> None:
        """Apply a camera tuning preset (see CameraPreset)."""
        self._command(CMD_CAM_PRESET, bytes([preset & 0xFF]))

    def camera_set_param(self, param_id: int, value: int) -> None:
        """Set a single camera parameter by id."""
        self._command(CMD_CAM_SET_PARAM, struct.pack("<BH", param_id & 0xFF, value & 0xFFFF))

    def camera_get_params(self) -> bytes:
        """Return raw camera parameter block from firmware."""
        resp = self._command(CMD_CAM_GET_PARAMS)
        return resp or b""

    def led_mode(self, mode: int) -> None:
        """Set RGB ring mode (off, solid, breathe, …)."""
        self._command(CMD_LED_MODE, bytes([mode & 0xFF]))

    def led_brightness(self, percent: int) -> None:
        """Set RGB ring brightness 0–100."""
        self._command(CMD_LED_BRIGHTNESS, bytes([max(0, min(100, percent))]))

    def led_color(self, r: int, g: int, b: int) -> None:
        """Set RGB ring colour."""
        self._command(CMD_LED_COLOR, bytes([r & 0xFF, g & 0xFF, b & 0xFF]))

    def led_illumination(self, percent: int) -> None:
        """Set front illumination LED 0–100."""
        self._command(CMD_LED_ILLUMINATION, bytes([max(0, min(100, percent))]))

    def config_save(self) -> None:
        """Persist runtime config to flash."""
        self._command(CMD_CONFIG_SAVE)

    def config_load(self) -> None:
        """Reload config from flash."""
        self._command(CMD_CONFIG_LOAD)

    def config_reset(self) -> None:
        """Restore factory defaults."""
        self._command(CMD_CONFIG_RESET)

    def aruco_set_dictionary(self, dictionary_id: int) -> None:
        """Select ArUco dictionary id."""
        self._command(CMD_ARUCO_SET_DICT, bytes([dictionary_id & 0xFF]))

    def motion_sensitivity(self, level: int) -> None:
        """Set optical-flow / motion sensitivity 0–100."""
        self._command(CMD_MOTION_SENSITIVITY, bytes([max(0, min(100, level))]))

    # ── Benchmarking ────────────────────────────────────────────────────

    def bench_start(self) -> None:
        """Enable on-device profiling."""
        self._command(CMD_BENCH_ENABLE, b"\x01")

    def bench_stop(self) -> None:
        """Disable on-device profiling."""
        self._command(CMD_BENCH_ENABLE, b"\x00")

    def bench_reset(self) -> None:
        """Reset benchmark counters."""
        self._command(CMD_BENCH_RESET)

    def bench_report(self) -> Optional[PerfReport]:
        """Get benchmark report from device."""
        resp = self._command(CMD_BENCH_REPORT)
        if not resp or len(resp) < 8:
            return None
        return self._parse_bench_report(resp)

    def bench_print(self) -> None:
        """Print benchmark report to device debug UART."""
        self._command(CMD_BENCH_PRINT)

    # ── Performance query ───────────────────────────────────────────────

    def get_perf(self) -> dict:
        """Get quick performance stats."""
        resp = self._command(CMD_GET_PERF)
        if not resp or len(resp) < 12:
            return {}
        fps, infer, decode, cpu, kpu_mhz, frames = struct.unpack_from("<HHHHHH", resp)
        return {
            "fps": fps / 10.0,
            "inference_us": infer,
            "decode_us": decode,
            "cpu_percent": cpu / 10.0,
            "kpu_mhz": kpu_mhz,
            "frame_count": frames,
        }

    # ── Internal ────────────────────────────────────────────────────────

    def _command(self, cmd: int, data: bytes = b"",
                 timeout: float = None) -> Optional[bytes]:
        """Send a DLP command and wait for response."""
        if timeout is None:
            timeout = self._timeout

        pkt = build_packet(cmd, data)

        with self._lock:
            self._ser.reset_input_buffer()
            self._ser.write(pkt)

            deadline = time.monotonic() + timeout
            buf = bytearray()

            while time.monotonic() < deadline:
                chunk = self._ser.read(256)
                if chunk:
                    buf.extend(chunk)
                    result = parse_packet(buf)
                    if result:
                        resp_cmd, resp_data, consumed = result
                        return resp_data

            raise DristyTimeout(f"No response to cmd 0x{cmd:02X} within {timeout}s")

    def _parse_result_response(self, data: bytes) -> Frame:
        """Parse CMD_GET_RESULT response into a Frame."""
        f = Frame()
        if len(data) < 16:
            return f

        off = 0
        f.frame_number = struct.unpack_from("<H", data, off)[0]; off += 2
        fps_x10 = struct.unpack_from("<H", data, off)[0]; off += 2
        f.fps = fps_x10 / 10.0
        f.pipeline_us = struct.unpack_from("<H", data, off)[0]; off += 2
        f.inference_us = struct.unpack_from("<H", data, off)[0]; off += 2
        mode_byte = data[off]; off += 1
        try:
            f.mode = Mode(mode_byte)
        except ValueError:
            pass
        det_count = data[off]; off += 1
        track_count = data[off]; off += 1
        tag_count = data[off]; off += 1
        blob_count = data[off]; off += 1
        flow_count = data[off]; off += 1
        f.n_detections = det_count
        f.n_tracks = track_count
        f.n_tags = tag_count
        f.n_blobs = blob_count
        f.n_flow = flow_count
        # New firmware (>=22 bytes) inserts qr/line/motion before target.
        if len(data) >= 22:
            f.n_qr = data[off]; off += 1
            f.line_valid = bool(data[off]); off += 1
            mp = struct.unpack_from("<H", data, off)[0]; off += 2
            f.motion_percent = mp / 10.0
        target_type = data[off] if off < len(data) else 0
        off += 1
        if off + 4 <= len(data):
            error_x = struct.unpack_from("<h", data, off)[0]; off += 2
            error_y = struct.unpack_from("<h", data, off)[0]; off += 2
            f.target = Target(
                type=TargetType(target_type) if target_type < 6 else TargetType.NONE,
                error_x=error_x,
                error_y=error_y,
            )

        return f

    def _parse_ctrl_target(self, data: bytes) -> Target:
        """Parse control output target packet."""
        # Skip the wire header if present (sync + type + len)
        if len(data) >= 25 and data[0] == 0xD5:
            data = data[4:]  # skip sync + type + len(2)

        if len(data) < 20:
            return Target()

        ts, ex, ey, sz, drx, dry, rng, tid, ttype, conf = struct.unpack_from(
            "<IhhHhhHHBB", data)
        return Target(
            timestamp_ms=ts,
            error_x=ex,
            error_y=ey,
            size=sz,
            error_rate_x=drx,
            error_rate_y=dry,
            range_mm=rng,
            track_id=tid,
            type=TargetType(ttype) if ttype < 6 else TargetType.NONE,
            confidence=conf,
        )

    def _parse_tracks(self, data: bytes) -> list:
        """Parse track wire format (16 bytes per track)."""
        tracks = []
        off = 0
        while off + 16 <= len(data):
            tid, cx, cy, w, h, vx, vy, age = struct.unpack_from("<HhhhhhhH", data, off)
            tracks.append(Track(
                id=tid, cx=cx, cy=cy, vel_x=vx, vel_y=vy,
            ))
            off += 16
        return tracks

    def _parse_tags(self, data: bytes) -> list:
        """Parse tag wire format (20 bytes per tag)."""
        tags = []
        off = 0
        while off + 20 <= len(data):
            tid, fam, cx, cy, ham, tx, ty, tz, yaw = struct.unpack_from(
                "<HHhhHhhhh", data, off)
            tags.append(Tag(
                tag_id=tid, family=fam, cx=cx, cy=cy, hamming=ham,
                range_mm=int((tx**2 + ty**2 + tz**2)**0.5) if tz > 0 else 0,
                yaw_deg=yaw / 10.0,
            ))
            off += 20
        return tags

    def _parse_blobs(self, data: bytes) -> list:
        """Parse blob wire format (10 bytes per blob)."""
        blobs = []
        off = 0
        while off + 10 <= len(data):
            cx, cy, w, h = struct.unpack_from("<hhhh", data, off)
            colour_id = data[off + 8]
            density = data[off + 9] / 100.0
            blobs.append(Blob(cx=cx, cy=cy, w=w, h=h,
                              colour_id=colour_id, density=density))
            off += 10
        return blobs

    def _parse_qr(self, data: bytes) -> list:
        """Parse length-prefixed UTF-8 QR strings."""
        strings = []
        off = 0
        while off < len(data):
            slen = data[off]
            off += 1
            if off + slen > len(data):
                break
            strings.append(data[off:off + slen].decode("utf-8", errors="replace"))
            off += slen
        return strings

    def _parse_flow(self, data: bytes) -> list:
        """Parse flow wire format (8 bytes per cell)."""
        cells = []
        off = 0
        while off + 8 <= len(data):
            dx, dy, rcx, rcy = struct.unpack_from("<hhhh", data, off)
            cells.append(FlowCell(roi_cx=rcx, roi_cy=rcy, dx=dx / 100.0, dy=dy / 100.0))
            off += 8
        return cells

    def _parse_bench_report(self, data: bytes) -> PerfReport:
        """Parse serialised benchmark report."""
        if len(data) < 8:
            return PerfReport()

        frames = struct.unpack_from("<H", data, 2)[0]
        fps_x10 = struct.unpack_from("<H", data, 4)[0]
        kpu_x10 = struct.unpack_from("<H", data, 6)[0]

        stages = {}
        stage_names = [
            "frame_total", "dvp_capture", "kpu_inference", "yolo_decode",
            "nms", "tracker", "apriltag", "aruco", "motion", "flow",
            "colour", "qr", "pose", "target_select", "result_bus",
            "dlp_serialize", "lcd_update", "idle",
        ]
        off = 8
        for name in stage_names:
            if off + 8 > len(data):
                break
            mean, max_v, p95, count = struct.unpack_from("<HHHH", data, off)
            if count > 0:
                stages[name] = {
                    "mean_us": mean, "max_us": max_v,
                    "p95_us": p95, "count": count,
                }
            off += 8

        return PerfReport(
            frames=frames,
            fps=fps_x10 / 10.0,
            kpu_util=kpu_x10 / 10.0,
            stages=stages,
        )
