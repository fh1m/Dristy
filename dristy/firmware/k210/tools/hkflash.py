#!/usr/bin/env python3
"""Dristy USB-serial flashing and monitor tool."""

from __future__ import annotations

import argparse
import binascii
import hashlib
from pathlib import Path
import struct
import sys
import time


ROOT = Path(__file__).resolve().parents[1]
TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from board_contract import Board, ContractError, load_board
from firmware_sidecar import SidecarError, read_and_validate as read_and_validate_sidecar
from gen_flash_layout import load_validated, partition_by_name

FLASH_CHUNK = 4096
SRAM_CHUNK = 1024
STUB_ADDRESS = 0x80000000

BOOT_RET_OK = 0xE0
FLASH_RET_OK = 0xE0
FLASH_RET_BUSY = 0xE7

OP_MEMORY_WRITE = 0xC3
OP_MEMORY_BOOT = 0xC5
OP_BOOT_NOP = 0xC2
OP_FLASH_NOP = 0xD2
OP_FLASH_WRITE = 0xD4
OP_FLASH_REBOOT = 0xD5
OP_FLASH_BAUDRATE = 0xD6
OP_FLASH_INIT = 0xD7
OP_FLASH_ERASE_NONBLOCKING = 0xD8
OP_FLASH_STATUS = 0xD9


class ProtocolError(RuntimeError):
    pass


def require_serial():
    try:
        import serial
        import serial.tools.list_ports
    except Exception as exc:  # pragma: no cover - local setup path
        raise SystemExit(
            "pyserial is required. Install with: python -m pip install pyserial"
        ) from exc
    return serial


def slip_encode(packet: bytes) -> bytes:
    return b"\xc0" + packet.replace(b"\xdb", b"\xdb\xdd").replace(b"\xc0", b"\xdb\xdc") + b"\xc0"


def slip_read(ser, timeout: float = 3.0) -> bytes:
    deadline = time.monotonic() + timeout
    data = bytearray()
    in_frame = False
    in_escape = False

    while time.monotonic() < deadline:
        b = ser.read(1)
        if not b:
            continue
        c = b[0]
        if not in_frame:
            if c == 0xC0:
                in_frame = True
            continue
        if in_escape:
            in_escape = False
            if c == 0xDC:
                data.append(0xC0)
            elif c == 0xDD:
                data.append(0xDB)
            else:
                raise ProtocolError(f"bad SLIP escape byte: 0x{c:02x}")
        elif c == 0xDB:
            in_escape = True
        elif c == 0xC0:
            if data:
                return bytes(data)
        else:
            data.append(c)

    raise TimeoutError("timed out waiting for SLIP frame")


def parse_response(data: bytes) -> tuple[int, int, str | None]:
    if len(data) < 2:
        raise ProtocolError(f"short response: {data.hex()}")
    op = data[0]
    reason = data[1]
    text = None
    if len(data) > 2:
        text = data[2:].decode(errors="replace").strip() or None
    return op, reason, text


def request(op: int, payload: bytes = b"") -> bytes:
    crc = binascii.crc32(payload) & 0xFFFFFFFF
    return struct.pack("<HHI", op, 0, crc) + payload


def expect_response(
    ser,
    expected_op: int,
    ok_reasons: tuple[int, ...] = (BOOT_RET_OK,),
    timeout: float = 3.0,
    label: str = "command",
) -> tuple[int, int, str | None]:
    op, reason, text = parse_response(slip_read(ser, timeout=timeout))
    if op != expected_op or reason not in ok_reasons:
        detail = f"op=0x{op:02x} reason=0x{reason:02x}"
        if text:
            detail += f" text={text}"
        raise ProtocolError(f"{label} failed: {detail}")
    return op, reason, text


def progress(prefix: str, done: int, total: int) -> None:
    pct = 100.0 if total == 0 else done * 100.0 / total
    sys.stdout.write(f"\r{prefix} {done}/{total} bytes ({pct:5.1f}%)")
    sys.stdout.flush()
    if done >= total:
        sys.stdout.write("\n")


def cmd_list(_args: argparse.Namespace) -> int:
    serial = require_serial()
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        print("No serial ports found.")
        return 1

    for port in ports:
        print(f"{port.device}\t{port.description}\t{port.hwid}")
    return 0


def resolve_port(args: argparse.Namespace, serial) -> str:
    if args.port:
        return args.port

    if args.usb_detection_mode == "explicit-port":
        raise SystemExit("[ERR] selected board requires --port explicitly")
    try:
        port = next(serial.tools.list_ports.grep(args.usb_vid_regex))
    except StopIteration as exc:
        raise SystemExit(
            f"[ERR] no board-compatible USB serial port found by VID regex {args.usb_vid_regex}. "
            "Pass --port COMx explicitly."
        ) from exc

    print(f"[SERIAL] auto-selected {port.device}: {port.description}")
    return port.device


def reset_to_isp_dan(ser, pulse: float = 0.01) -> None:
    ser.dtr = False
    ser.rts = False
    time.sleep(pulse)
    ser.dtr = False
    ser.rts = True
    time.sleep(pulse)
    ser.rts = False
    ser.dtr = True
    time.sleep(pulse)


def reset_to_isp_kd233(ser, pulse: float = 0.01) -> None:
    ser.dtr = False
    ser.rts = False
    time.sleep(pulse)
    ser.dtr = True
    ser.rts = False
    time.sleep(pulse)
    ser.rts = True
    ser.dtr = False
    time.sleep(pulse)


def reset_to_boot_uploader(ser, pulse: float = 0.01) -> None:
    ser.dtr = False
    ser.rts = False
    time.sleep(pulse)
    ser.dtr = True
    ser.rts = False
    time.sleep(pulse)
    ser.rts = False
    ser.dtr = False
    time.sleep(pulse)


def pulse_auto_reset(args: argparse.Namespace, ser) -> None:
    if not args.auto_reset:
        return

    boot_value = not args.invert_rts
    reset_value = not args.invert_dtr

    if args.boot_rts:
        ser.rts = boot_value
    if args.reset_dtr:
        ser.dtr = reset_value
    time.sleep(0.05)
    if args.reset_dtr:
        ser.dtr = not reset_value
    time.sleep(0.05)
    if args.boot_rts:
        ser.rts = not boot_value


def clear_serial_buffers(ser) -> None:
    try:
        ser.reset_input_buffer()
        ser.reset_output_buffer()
    except Exception:
        pass


def open_runtime_serial(serial, port: str, baud: int, timeout: float = 0.1, dtr: bool = False, rts: bool = False):
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = baud
    ser.timeout = timeout
    ser.write_timeout = 2.0
    ser.rtscts = False
    ser.dsrdtr = False
    ser.dtr = dtr
    ser.rts = rts
    ser.open()
    ser.dtr = dtr
    ser.rts = rts
    time.sleep(0.05)
    return ser


def manual_wait() -> None:
    print("[FLASH] manual mode:")
    print("        1. Hold BOOT/ISP as wired for the board.")
    print("        2. Reset/power-cycle the K210 while BOOT/ISP is asserted.")
    print("        3. Release reset, keep BOOT/ISP if required, then press Enter here.")
    input()


class K210Isp:
    def __init__(self, ser) -> None:
        self.ser = ser

    def write_packet(self, packet: bytes) -> None:
        self.ser.write(slip_encode(packet))

    def bootrom_greeting(self, retries: int = 10, timeout: float = 3.0) -> None:
        pkt = b"\xc2" + b"\x00" * 12
        last_error: Exception | None = None
        for _ in range(retries):
            self.ser.write(b"\xc0" + pkt + b"\xc0")
            try:
                expect_response(self.ser, OP_BOOT_NOP, timeout=timeout, label="bootrom greeting")
                return
            except Exception as exc:
                last_error = exc
                time.sleep(0.1)
        raise ProtocolError(f"bootrom greeting failed: {last_error}")

    def memory_write(self, address: int, data: bytes) -> None:
        done = 0
        total = len(data)
        while done < total:
            chunk = data[done : done + SRAM_CHUNK]
            payload = struct.pack("<II", address + done, len(chunk)) + chunk
            self.write_packet(request(OP_MEMORY_WRITE, payload))
            expect_response(self.ser, OP_MEMORY_WRITE, label="SRAM write")
            done += len(chunk)
            progress("[FLASH] ISP stub", done, total)

    def boot_sram(self, address: int = STUB_ADDRESS) -> None:
        payload = struct.pack("<II", address, 0)
        self.write_packet(request(OP_MEMORY_BOOT, payload))

    def flash_greeting(self, retries: int = 20) -> None:
        pkt = b"\xd2" + b"\x00" * 12
        last_error: Exception | None = None
        for _ in range(retries):
            self.ser.write(b"\xc0" + pkt + b"\xc0")
            try:
                expect_response(self.ser, OP_FLASH_NOP, ok_reasons=(FLASH_RET_OK,), label="flash stub greeting")
                return
            except Exception as exc:
                last_error = exc
                time.sleep(0.1)
        raise ProtocolError(f"flash stub greeting failed: {last_error}")

    def change_baudrate(self, baudrate: int) -> None:
        payload = struct.pack("<III", 0, 4, baudrate)
        self.write_packet(request(OP_FLASH_BAUDRATE, payload))
        time.sleep(0.05)
        self.ser.baudrate = baudrate

    def init_flash(self, flash_type: int) -> None:
        payload = struct.pack("<II", flash_type, 0)
        self.write_packet(request(OP_FLASH_INIT, payload))
        expect_response(
            self.ser,
            OP_FLASH_INIT,
            ok_reasons=(FLASH_RET_OK,),
            timeout=5.0,
            label="flash init",
        )

    def erase_flash(self, address: int, length: int) -> None:
        payload = struct.pack("<II", address, length)
        self.write_packet(request(OP_FLASH_ERASE_NONBLOCKING, payload))
        expect_response(
            self.ser,
            OP_FLASH_ERASE_NONBLOCKING,
            ok_reasons=(FLASH_RET_OK, FLASH_RET_BUSY),
            timeout=90.0,
            label="flash erase",
        )

        while True:
            self.ser.write(b"\xc0\xd9" + b"\x00" * 12 + b"\xc0")
            op, reason, text = parse_response(slip_read(self.ser, timeout=90.0))
            if op == 0xD1 and text:
                print(f"[FLASH] {text}")
                continue
            if op == OP_FLASH_STATUS and reason == FLASH_RET_OK:
                return
            if op == OP_FLASH_STATUS and reason == FLASH_RET_BUSY:
                print("[FLASH] erase busy")
                time.sleep(2.0)
                continue
            raise ProtocolError(f"flash status failed: op=0x{op:02x} reason=0x{reason:02x}")

    def write_flash(self, address: int, data: bytes) -> None:
        done = 0
        total = len(data)
        while done < total:
            chunk = data[done : done + FLASH_CHUNK]
            wire_chunk = chunk.ljust(FLASH_CHUNK, b"\x00")
            payload = struct.pack("<II", address + done, len(wire_chunk)) + wire_chunk
            self.write_packet(request(OP_FLASH_WRITE, payload))

            while True:
                op, reason, text = parse_response(slip_read(self.ser, timeout=90.0))
                if op == OP_FLASH_WRITE and reason == FLASH_RET_OK:
                    break
                if op == OP_FLASH_WRITE and reason == FLASH_RET_BUSY:
                    time.sleep(0.5)
                    continue
                if text:
                    raise ProtocolError(f"flash write failed: {text}")
                raise ProtocolError(f"flash write failed: op=0x{op:02x} reason=0x{reason:02x}")

            done += len(chunk)
            progress("[FLASH] image", done, total)

    def reboot(self, fallback_reset=None) -> None:
        self.ser.write(b"\xc0\xd5" + b"\x00" * 12 + b"\xc0")
        try:
            expect_response(
                self.ser,
                OP_FLASH_REBOOT,
                ok_reasons=(FLASH_RET_OK, FLASH_RET_BUSY),
                timeout=10.0,
                label="reboot",
            )
        except (TimeoutError, ProtocolError) as exc:
            if not fallback_reset:
                if isinstance(exc, TimeoutError):
                    return
                raise
            print(f"[WARN] ISP reboot command failed ({exc}); using descriptor reboot profile")
            fallback_reset(self.ser)


def make_k210_image_payload(raw: bytes, io_mode: str = "qio") -> bytes:
    flag = 0x02 if io_mode == "dio" else 0x00
    data = bytes([flag]) + struct.pack("<I", len(raw)) + raw
    return data + hashlib.sha256(data).digest()


def round_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) // alignment * alignment


def firmware_erase_length(payload_size: int, *, flash_address: int,
                          firmware_flash_limit: int,
                          flash_erase_size: int) -> int:
    if payload_size <= 0:
        raise ValueError("firmware payload is empty")
    erase_len = round_up(payload_size, flash_erase_size)
    if flash_address + erase_len > firmware_flash_limit:
        raise ValueError(
            "firmware payload exceeds canonical partition: "
            f"payload={payload_size} erase={erase_len} "
            f"limit=0x{firmware_flash_limit:06X}"
        )
    return erase_len


def validate_image_sidecar(image: Path, args: argparse.Namespace) -> None:
    default_sidecar = image.with_suffix(".json")
    selected_sidecar = (
        Path(args.sidecar) if args.sidecar else default_sidecar
    )

    # A same-stem sidecar is evidence attached to the image even when the
    # caller supplies an alternate path.  Validate every existing candidate so
    # ``--sidecar missing --allow-missing-sidecar`` cannot hide stale or
    # mismatched metadata beside the image.
    candidates: list[Path] = [default_sidecar]
    if selected_sidecar.resolve() != default_sidecar.resolve():
        candidates.append(selected_sidecar)
    for sidecar in candidates:
        if not sidecar.is_file():
            continue
        try:
            read_and_validate_sidecar(
                sidecar,
                image,
                args.board_descriptor,
                expected_flash_address=args.flash_address,
            )
        except SidecarError as exc:
            raise ValueError(
                f"firmware sidecar mismatch ({sidecar}): {exc}"
            ) from exc

    if selected_sidecar.is_file():
        return
    if args.allow_missing_sidecar:
        print(
            "[WARN] selected sidecar missing under explicit raw-image "
            f"override: {selected_sidecar}"
        )
        return
    raise ValueError(
        f"firmware sidecar is required: {selected_sidecar}; "
        "use --allow-missing-sidecar only for a trusted raw image"
    )


def apply_runtime_reboot_profile(args: argparse.Namespace, ser) -> None:
    handler = getattr(args, "runtime_reboot_handler", None)
    if handler is None:
        profile = getattr(args, "reboot_profile", None)
        raise ProtocolError(f"reboot profile {profile!r} has no implementation")
    handler(ser)


def cmd_monitor(args: argparse.Namespace) -> int:
    serial = require_serial()
    port = resolve_port(args, serial)
    monitor_baud = args.monitor_baud
    duration = getattr(args, "duration", None)
    with serial.Serial(port, monitor_baud, timeout=0.1) as ser:
        if getattr(args, "reset_before_read", False):
            clear_serial_buffers(ser)
            apply_runtime_reboot_profile(args, ser)
        print(f"[MON] {port} @ {monitor_baud}. Ctrl+C to stop.", flush=True)
        deadline = None if duration is None else time.monotonic() + duration
        try:
            while deadline is None or time.monotonic() < deadline:
                data = ser.readline()
                if data:
                    timestamp = time.strftime("%H:%M:%S")
                    text = data.decode(errors="replace").rstrip()
                    print(f"{timestamp} {text}", flush=True)
        except KeyboardInterrupt:
            print("\n[MON] stopped")
    if duration is not None:
        print("[MON] stopped")
    return 0


def cmd_flash(args: argparse.Namespace) -> int:
    image = Path(args.image)
    if not image.is_file():
        print(f"[ERR] image not found: {image}", file=sys.stderr)
        return 1

    print(f"[FLASH] image: {image}")
    print(f"[FLASH] size: {image.stat().st_size} bytes")

    raw_image = image.read_bytes()
    flash_payload = make_k210_image_payload(raw_image, io_mode=args.io_mode)
    try:
        erase_len = firmware_erase_length(
            len(flash_payload),
            flash_address=args.flash_address,
            firmware_flash_limit=args.firmware_flash_limit,
            flash_erase_size=args.flash_erase_size,
        )
        validate_image_sidecar(image, args)
    except ValueError as exc:
        print(f"[ERR] {exc}", file=sys.stderr)
        return 1

    stub = Path(args.isp_stub)
    if not stub.is_file():
        print(f"[ERR] ISP stub not found: {stub}", file=sys.stderr)
        print("      Run: python dristy\\tools\\bootstrap_deps.py", file=sys.stderr)
        return 1

    serial = require_serial()
    port = resolve_port(args, serial)
    print(f"[FLASH] port: {port}")

    with serial.Serial(port, args.boot_baud, timeout=0.1) as ser:
        if args.manual:
            manual_wait()
        ser.timeout = 0.1
        isp = K210Isp(ser)
        try:
            print("[FLASH] connecting to K210 BootROM")
            if args.manual:
                isp.bootrom_greeting(timeout=3.0)
            elif args.auto_reset:
                pulse_auto_reset(args, ser)
                isp.bootrom_greeting(timeout=3.0)
            else:
                connector = args.reset_connector
                if connector is None:
                    raise ProtocolError(
                        f"reset profile {args.reset_profile!r} has no implementation"
                    )
                connector(isp, ser, attempts=args.reset_attempts)
            print("[FLASH] loading ISP stub")
            isp.memory_write(STUB_ADDRESS, stub.read_bytes())
            isp.boot_sram(STUB_ADDRESS)
            time.sleep(0.3)
            print("[FLASH] connecting to ISP stub")
            isp.flash_greeting()
            if args.flash_baud != args.boot_baud:
                print(f"[FLASH] switch UART {args.boot_baud} -> {args.flash_baud}")
                isp.change_baudrate(args.flash_baud)
            isp.init_flash(args.flash_type)
            if args.erase:
                print(f"[FLASH] erase 0x{args.flash_address:06x}+0x{erase_len:x}")
                isp.erase_flash(args.flash_address, erase_len)
            print(f"[FLASH] write 0x{args.flash_address:06x}+0x{len(flash_payload):x}")
            isp.write_flash(args.flash_address, flash_payload)
            if args.verify:
                print("[WARN] verify readback is not implemented: this ISP stub has no flash-read command exposed.")
            if args.reboot:
                print("[FLASH] reboot")
                isp.reboot(fallback_reset=args.reboot_fallback_handler)
        except (TimeoutError, ProtocolError) as exc:
            print(f"[ERR] {exc}", file=sys.stderr)
            return 2
    print("[OK] flash complete")
    return 0


def cmd_flash_monitor(args: argparse.Namespace) -> int:
    rc = cmd_flash(args)
    if rc != 0:
        return rc
    if getattr(args, "monitor_reset", True):
        args.reset_before_read = True
    return cmd_monitor(args)


def read_serial_line(ser, timeout: float) -> bytes:
    deadline = time.monotonic() + timeout
    line = bytearray()
    while time.monotonic() < deadline:
        b = ser.read(1)
        if not b:
            continue
        if b in (b"\n", b"\r"):
            return bytes(line)
        line.extend(b)
        if len(line) > 512:
            line.clear()
    raise TimeoutError("serial line timeout")


def read_exact(ser, size: int, timeout: float, label: str = "data", progress_prefix: str = "[DATA]") -> bytes:
    deadline = time.monotonic() + timeout
    data = bytearray()
    last_print = 0
    while len(data) < size and time.monotonic() < deadline:
        chunk = ser.read(min(4096, size - len(data)))
        if not chunk:
            continue
        data.extend(chunk)
        if len(data) - last_print >= 8192 or len(data) == size:
            progress(f"{progress_prefix} {label}", len(data), size)
            last_print = len(data)
    if len(data) != size:
        raise TimeoutError(f"{label} timeout: got {len(data)} of {size} bytes")
    return bytes(data)


def bmp24_from_rgb565_le(payload: bytes, width: int, height: int) -> bytes:
    row_bytes = ((width * 3) + 3) & ~3
    file_size = 54 + row_bytes * height
    out = bytearray(file_size)
    struct.pack_into("<2sIHHI", out, 0, b"BM", file_size, 0, 0, 54)
    struct.pack_into("<IIIHHIIIIII", out, 14, 40, width, height, 1, 24, 0, row_bytes * height, 2835, 2835, 0, 0)
    for y in range(height):
        src_y = height - 1 - y
        row_off = 54 + y * row_bytes
        src_off = src_y * width * 2
        for x in range(width):
            value = payload[src_off + x * 2] | (payload[src_off + x * 2 + 1] << 8)
            r = ((value >> 11) & 0x1F) * 255 // 31
            g = ((value >> 5) & 0x3F) * 255 // 63
            b = (value & 0x1F) * 255 // 31
            dst = row_off + x * 3
            out[dst : dst + 3] = bytes((b, g, r))
    return bytes(out)


def cmd_screenshot(args: argparse.Namespace) -> int:
    serial = require_serial()
    port = resolve_port(args, serial)
    out = Path(args.output) if args.output else ROOT / "screenshots" / time.strftime("screen_%Y%m%d_%H%M%S.bmp")
    out.parent.mkdir(parents=True, exist_ok=True)

    with open_runtime_serial(serial, port, args.baud, timeout=0.1, dtr=args.runtime_dtr, rts=args.runtime_rts) as ser:
        if args.reset_before_read:
            clear_serial_buffers(ser)
            apply_runtime_reboot_profile(args, ser)
            time.sleep(args.reset_wait)

        clear_serial_buffers(ser)
        print(f"[SHOT] request HKSHOT on {port} @ {args.baud}")
        ser.write(b"HKSHOT\r\n")
        ser.flush()

        header = b""
        deadline = time.monotonic() + args.timeout
        while time.monotonic() < deadline:
            try:
                line = read_serial_line(ser, timeout=1.0)
            except TimeoutError:
                continue
            if not line:
                continue
            text = line.decode(errors="replace")
            if text.startswith("HKSHOT BEGIN "):
                header = line
                break
            if args.verbose:
                print(f"[UART] {text}")
        if not header:
            print("[ERR] HKSHOT header not received", file=sys.stderr)
            return 2

        parts = header.decode("ascii", errors="strict").split()
        if len(parts) != 6 or parts[0] != "HKSHOT" or parts[1] != "BEGIN":
            print(f"[ERR] bad HKSHOT header: {header!r}", file=sys.stderr)
            return 2
        fmt = parts[2]
        width = int(parts[3])
        height = int(parts[4])
        size = int(parts[5])
        if fmt != "BMP24" or width <= 0 or height <= 0 or size <= 0:
            print(f"[ERR] unsupported HKSHOT header: {header!r}", file=sys.stderr)
            return 2

        payload = read_exact(ser, size, timeout=args.timeout, label=f"{width}x{height} BMP", progress_prefix="[SHOT]")
        footer = b""
        footer_deadline = time.monotonic() + 5.0
        while time.monotonic() < footer_deadline:
            try:
                line = read_serial_line(ser, timeout=1.0)
            except TimeoutError:
                continue
            if not line:
                continue
            if line.startswith(b"HKSHOT END "):
                footer = line
                break
        if not footer:
            print("[ERR] HKSHOT footer not received", file=sys.stderr)
            return 2

        expected_crc = int(footer.decode("ascii").split()[2], 16)
        actual_crc = binascii.crc32(payload) & 0xFFFFFFFF
        if actual_crc != expected_crc:
            print(f"[ERR] CRC mismatch: expected {expected_crc:08X}, got {actual_crc:08X}", file=sys.stderr)
            return 2

        out.write_bytes(payload)
        print(f"[OK] screenshot saved: {out}")
    return 0


def cmd_uart_cmd(args: argparse.Namespace) -> int:
    serial = require_serial()
    port = resolve_port(args, serial)
    commands = [line.strip() for line in args.command.splitlines() if line.strip()]

    with open_runtime_serial(serial, port, args.baud, timeout=0.1, dtr=args.runtime_dtr, rts=args.runtime_rts) as ser:
        if args.send_delay > 0:
            time.sleep(args.send_delay)
        clear_serial_buffers(ser)
        print(f"[CMD] {len(commands)} command(s) on {port} @ {args.baud}")
        for index, command in enumerate(commands):
            print(f"[CMD] > {command}")
            ser.write((command + "\n").encode("ascii"))
            ser.flush()
            if index + 1 < len(commands) and args.line_delay > 0:
                time.sleep(args.line_delay)

        deadline = time.monotonic() + args.duration
        while time.monotonic() < deadline:
            try:
                line = read_serial_line(ser, timeout=0.5)
            except TimeoutError:
                continue
            if line:
                print(line.decode(errors="replace"))
    return 0


def cmd_frame(args: argparse.Namespace) -> int:
    serial = require_serial()
    port = resolve_port(args, serial)
    out = Path(args.output) if args.output else ROOT / "frames" / time.strftime("frame_%Y%m%d_%H%M%S.raw")
    raw_out = out if out.suffix.lower() == ".raw" else out.with_suffix(".raw")
    bmp_out = out.with_suffix(".bmp")
    raw_out.parent.mkdir(parents=True, exist_ok=True)

    with open_runtime_serial(serial, port, args.baud, timeout=0.1, dtr=args.runtime_dtr, rts=args.runtime_rts) as ser:
        if args.send_delay > 0:
            time.sleep(args.send_delay)
        clear_serial_buffers(ser)
        print(f"[FRAME] request HKFRAME on {port} @ {args.baud}")
        ser.write(b"HKFRAME\n")
        ser.flush()

        header = b""
        deadline = time.monotonic() + args.timeout
        while time.monotonic() < deadline:
            try:
                line = read_serial_line(ser, timeout=1.0)
            except TimeoutError:
                continue
            if not line:
                continue
            text = line.decode(errors="replace")
            if text.startswith("HKFRAME ERR"):
                print(f"[ERR] {text}", file=sys.stderr)
                return 2
            if text.startswith("HKFRAME BEGIN "):
                header = line
                break
            if args.verbose:
                print(f"[UART] {text}")
        if not header:
            print("[ERR] HKFRAME header not received", file=sys.stderr)
            return 2

        parts = header.decode("ascii", errors="strict").split()
        if len(parts) != 6 or parts[0] != "HKFRAME" or parts[1] != "BEGIN":
            print(f"[ERR] bad HKFRAME header: {header!r}", file=sys.stderr)
            return 2
        fmt = parts[2]
        width = int(parts[3])
        height = int(parts[4])
        size = int(parts[5])
        if fmt != "RGB565" or width <= 0 or height <= 0 or size != width * height * 2:
            print(f"[ERR] unsupported HKFRAME header: {header!r}", file=sys.stderr)
            return 2

        payload = read_exact(ser, size, timeout=args.timeout, label=f"{width}x{height} RGB565", progress_prefix="[FRAME]")
        footer = b""
        footer_deadline = time.monotonic() + 5.0
        while time.monotonic() < footer_deadline:
            try:
                line = read_serial_line(ser, timeout=1.0)
            except TimeoutError:
                continue
            if line.startswith(b"HKFRAME END "):
                footer = line
                break
        if not footer:
            print("[ERR] HKFRAME footer not received", file=sys.stderr)
            return 2

        expected_crc = int(footer.decode("ascii").split()[2], 16)
        actual_crc = binascii.crc32(payload) & 0xFFFFFFFF
        if actual_crc != expected_crc:
            print(f"[ERR] CRC mismatch: expected {expected_crc:08X}, got {actual_crc:08X}", file=sys.stderr)
            return 2

        raw_out.write_bytes(payload)
        bmp_out.write_bytes(bmp24_from_rgb565_le(payload, width, height))
        print(f"[OK] raw frame saved: {raw_out}")
        print(f"[OK] bmp preview saved: {bmp_out}")
    return 0


def connect_bootrom_uploader(isp: K210Isp, ser, attempts: int = 15) -> None:
    last_error: Exception | None = None
    for attempt in range(1, attempts + 1):
        print(f"[FLASH] ISP entry attempt {attempt}/{attempts}: dan reset")
        clear_serial_buffers(ser)
        reset_to_isp_dan(ser)
        try:
            isp.bootrom_greeting(retries=1, timeout=0.5)
            print("[FLASH] BootROM detected via dan reset")
            return
        except Exception as exc:
            last_error = exc

        print(f"[FLASH] ISP entry attempt {attempt}/{attempts}: kd233 reset")
        clear_serial_buffers(ser)
        reset_to_isp_kd233(ser)
        try:
            isp.bootrom_greeting(retries=1, timeout=0.5)
            print("[FLASH] BootROM detected via kd233 reset")
            return
        except Exception as exc:
            last_error = exc

    raise ProtocolError(f"uploader-compatible ISP entry failed: {last_error}")


RESET_PROFILE_CONNECTORS = {
    "dristy-uploader": connect_bootrom_uploader,
}

REBOOT_PROFILE_HANDLERS = {
    "uploader-normal": reset_to_boot_uploader,
}


def apply_board_configuration(args: argparse.Namespace) -> None:
    try:
        board = load_board(args.board)
    except ContractError as exc:
        raise SystemExit(f"[ERR] {exc}") from exc
    programming = board.programming
    hardware_affecting = args.cmd in {"flash", "flash-monitor"}
    if hardware_affecting and not programming["supported"]:
        raise SystemExit(
            f"[ERR] board {board.id!r} has programming.supported=false; flash is forbidden"
        )
    args.board_descriptor = board
    usb = programming["usb_detection"]
    args.usb_detection_mode = usb["mode"]
    args.usb_vid_regex = "|".join(f"({vid})" for vid in usb.get("vids", []))
    args.reboot_profile = programming.get("reboot_profile")
    args.runtime_reboot_handler = (
        REBOOT_PROFILE_HANDLERS.get(args.reboot_profile)
        if args.reboot_profile is not None else None
    )
    if args.reboot_profile is not None and args.runtime_reboot_handler is None:
        raise SystemExit(
            f"[ERR] reboot profile {args.reboot_profile!r} has no implementation"
        )

    if hardware_affecting:
        flash, partitions = load_validated(board.flash_layout_path)
        firmware = partition_by_name(partitions, "firmware")
        args.flash_address = firmware["offset"]
        args.firmware_flash_limit = firmware["offset"] + firmware["size"]
        args.flash_erase_size = flash["erase_size"]
        args.isp_stub = args.isp_stub or str(ROOT / programming["isp_stub"])
        args.boot_baud = args.boot_baud or programming["boot_baud"]
        args.flash_baud = args.flash_baud or programming["flash_baud"]
        flash_type = args.flash_type or programming["flash_type"]
        args.flash_type = {"spi0": 1}[flash_type]
        args.io_mode = args.io_mode or programming["flash_mode"]
        args.reset_attempts = args.reset_attempts or programming["reset_attempts"]
        args.reset_profile = programming["reset_profile"]
        if args.uploader_reset and args.reset_profile == "manual":
            raise SystemExit(
                "[ERR] --uploader-reset requires a descriptor reset profile implementation"
            )
        args.reset_connector = RESET_PROFILE_CONNECTORS.get(args.reset_profile)
        if args.reset_profile != "manual" and args.reset_connector is None:
            raise SystemExit(
                f"[ERR] reset profile {args.reset_profile!r} has no implementation"
            )
        if not args.manual and not args.auto_reset and args.reset_profile == "manual":
            args.manual = True
        fallback_enabled = args.uploader_reset_after_reboot
        if fallback_enabled is None:
            fallback_enabled = args.reboot_profile is not None
        args.reboot_fallback_handler = (
            args.runtime_reboot_handler if fallback_enabled else None
        )

    runtime_baud = programming.get("boot_baud")
    for attribute in ("baud", "monitor_baud"):
        if hasattr(args, attribute) and getattr(args, attribute) is None:
            if runtime_baud is None:
                raise SystemExit(
                    f"[ERR] board {board.id!r} has no runtime baud default; pass --baud"
                )
            setattr(args, attribute, runtime_baud)


def add_common_port_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--board", required=True, help="Canonical board.toml ID")
    parser.add_argument("--port", help="Serial port, for example COM5. Omit to auto-detect by Dristy Uploader VID list.")


def add_reset_args(parser: argparse.ArgumentParser) -> None:
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--manual", action="store_true", help="Wait for manual boot mode entry")
    mode.add_argument("--auto-reset", action="store_true", help="Try RTS/DTR reset sequencing")
    mode.add_argument(
        "--uploader-reset",
        "--dristy-reset",
        dest="uploader_reset",
        action="store_true",
        default=False,
        help="Use the selected board's named uploader reset implementation",
    )
    parser.add_argument("--boot-rts", action="store_true", help="Use RTS as BOOT control")
    parser.add_argument("--reset-dtr", action="store_true", help="Use DTR as RESET control")
    parser.add_argument("--invert-rts", action="store_true")
    parser.add_argument("--invert-dtr", action="store_true")
    parser.add_argument("--reset-attempts", type=int, help="Override descriptor reset attempts")


def add_flash_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--isp-stub", help="Safe override for descriptor ISP stub")
    parser.add_argument("--boot-baud", type=int, help="Override descriptor BootROM baudrate")
    parser.add_argument("--flash-baud", type=int, help="Override descriptor ISP flash baudrate")
    parser.add_argument("--flash-type", choices=["spi0"], help="Override descriptor flash type")
    parser.add_argument("--io-mode", choices=["dio", "qio"], help="Override descriptor flash mode")
    parser.add_argument("--sidecar", help="Firmware metadata sidecar; defaults to image stem + .json")
    parser.add_argument(
        "--allow-missing-sidecar",
        action="store_true",
        help=(
            "Allow an absent selected sidecar after layout/address/size checks; "
            "every existing selected or same-stem sidecar is still validated"
        ),
    )
    parser.add_argument("--erase", action="store_true", help="Run explicit flash erase before write; SEN0305 Uploader does not do this in the normal path")
    parser.add_argument("--verify", action="store_true", help="Request readback verification if supported")
    parser.add_argument("--no-reboot", dest="reboot", action="store_false", help="Do not reboot after flashing")
    parser.add_argument("--uploader-reset-after-reboot", dest="uploader_reset_after_reboot", action="store_true", help="Pulse uploader-style normal boot reset if ISP reboot is unsupported")
    parser.add_argument("--no-uploader-reset-after-reboot", dest="uploader_reset_after_reboot", action="store_false")
    parser.set_defaults(reboot=True, uploader_reset_after_reboot=None)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="hkflash", description="Dristy flash/debug tool")
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_list = sub.add_parser("list", help="List serial ports")
    p_list.set_defaults(func=cmd_list)

    p_monitor = sub.add_parser("monitor", help="Read UART logs")
    add_common_port_args(p_monitor)
    p_monitor.add_argument("--monitor-baud", "--baud", dest="monitor_baud", type=int)
    p_monitor.add_argument("--reset-before-read", action="store_true", help="Pulse uploader-style normal boot reset before reading")
    p_monitor.add_argument("--duration", type=float, help="Stop after this many seconds")
    p_monitor.set_defaults(func=cmd_monitor)

    p_screenshot = sub.add_parser("screenshot", help="Request a firmware LCD screenshot over UART")
    add_common_port_args(p_screenshot)
    p_screenshot.add_argument("-o", "--output", help="Output BMP path. Default: dristy/screenshots/screen_YYYYmmdd_HHMMSS.bmp")
    p_screenshot.add_argument("--baud", type=int, help="Firmware debug UART baudrate")
    p_screenshot.add_argument("--timeout", type=float, default=45.0, help="Screenshot transfer timeout")
    p_screenshot.add_argument("--reset-before-read", action="store_true", help="Pulse uploader-style normal boot reset before requesting screenshot")
    p_screenshot.add_argument("--reset-wait", type=float, default=2.5, help="Delay after reset before sending HKSHOT")
    p_screenshot.add_argument("--runtime-dtr", action="store_true", help="Keep DTR high while opening the runtime UART. Default keeps DTR low to avoid reset/boot glitches.")
    p_screenshot.add_argument("--runtime-rts", action="store_true", help="Keep RTS high while opening the runtime UART. Default keeps RTS low to avoid reset/boot glitches.")
    p_screenshot.add_argument("--verbose", action="store_true", help="Print non-HKSHOT UART lines seen while waiting")
    p_screenshot.set_defaults(func=cmd_screenshot)

    p_cmd = sub.add_parser("cmd", help="Send a text command to running Dristy firmware")
    add_common_port_args(p_cmd)
    p_cmd.add_argument("command", help="Command text, for example HKCAMINFO or HKCAMREGS")
    p_cmd.add_argument("--baud", type=int, help="Firmware debug UART baudrate")
    p_cmd.add_argument("--duration", type=float, default=2.0, help="How long to print response lines")
    p_cmd.add_argument("--send-delay", type=float, default=0.0, help="Delay after opening UART before sending the command")
    p_cmd.add_argument("--line-delay", type=float, default=0.0,
                       help="Delay between newline-separated commands")
    p_cmd.add_argument("--runtime-dtr", action="store_true", help="Keep DTR high while opening the runtime UART")
    p_cmd.add_argument("--runtime-rts", action="store_true", help="Keep RTS high while opening the runtime UART")
    p_cmd.set_defaults(func=cmd_uart_cmd)

    p_frame = sub.add_parser("frame", help="Request the current raw camera RGB565 frame over UART")
    add_common_port_args(p_frame)
    p_frame.add_argument("-o", "--output", help="Output RAW path. A BMP preview is written next to it.")
    p_frame.add_argument("--baud", type=int, help="Firmware debug UART baudrate")
    p_frame.add_argument("--timeout", type=float, default=60.0, help="Frame transfer timeout")
    p_frame.add_argument("--send-delay", type=float, default=0.0, help="Delay after opening UART before sending HKFRAME")
    p_frame.add_argument("--runtime-dtr", action="store_true", help="Keep DTR high while opening the runtime UART")
    p_frame.add_argument("--runtime-rts", action="store_true", help="Keep RTS high while opening the runtime UART")
    p_frame.add_argument("--verbose", action="store_true", help="Print non-HKFRAME UART lines seen while waiting")
    p_frame.set_defaults(func=cmd_frame)

    p_flash = sub.add_parser("flash", help="Flash a K210 image")
    p_flash.add_argument("image")
    add_common_port_args(p_flash)
    add_reset_args(p_flash)
    add_flash_args(p_flash)
    p_flash.set_defaults(func=cmd_flash)

    p_flash_monitor = sub.add_parser("flash-monitor", help="Flash then monitor UART logs")
    p_flash_monitor.add_argument("image")
    add_common_port_args(p_flash_monitor)
    add_reset_args(p_flash_monitor)
    add_flash_args(p_flash_monitor)
    p_flash_monitor.add_argument("--monitor-baud", "--baud", dest="monitor_baud", type=int)
    p_flash_monitor.add_argument("--duration", type=float, help="Stop monitor after this many seconds")
    p_flash_monitor.add_argument("--no-monitor-reset", dest="monitor_reset", action="store_false", help="Do not reset again before monitor")
    p_flash_monitor.set_defaults(monitor_reset=True)
    p_flash_monitor.set_defaults(func=cmd_flash_monitor)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.cmd != "list":
        apply_board_configuration(args)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
