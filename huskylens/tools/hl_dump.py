#!/usr/bin/env python3
"""Read the HUSKYLENS SPI flash back to a file.

No public tool does this. The stock kflash second-stage ISP is write-only, so
flashing custom firmware has always been a one-way door: you can restore
DFRobot's published packages, but you cannot recover the exact bytes that were
on your own device, including the model and service region DFRobot never
published.

This drives the extended ISP stub in ``firmware/hl-isp-dumper``, which adds
``ISP_FLASH_READ`` (0xD3) and ``ISP_FLASH_JEDEC`` (0xDA) to the existing
protocol.

The stub is booted into SRAM with ``kflash -s``, which does not write flash, so
taking a backup never risks the firmware you are backing up.

    python3 tools/hl_dump.py --port /dev/ttyUSB0 --output artifacts/stock_flash.bin
"""

from __future__ import annotations

import argparse
import binascii
import struct
import sys
import time
from pathlib import Path

import serial

REPO = Path(__file__).resolve().parent.parent
DEFAULT_STUB = REPO / "firmware" / "hl-isp-dumper" / "build" / "isp_prog_huskylens.bin"

SLIP_END = b"\xc0"
SLIP_ESC = b"\xdb"

#: Second-stage stub operations.
ISP_FLASH_READ = 0xD3
ISP_REBOOT = 0xD5
ISP_BAUDRATE = 0xD6
ISP_FLASH_INIT = 0xD7
ISP_FLASH_JEDEC = 0xDA

#: Mask-ROM BootROM operations.
ISP_BOOTROM_NOP = 0xC2
ISP_MEMORY_WRITE = 0xC3
ISP_MEMORY_BOOT = 0xC5

ISP_OK = 0xE0

#: The exact greeting frame kflash sends to the BootROM, pre-SLIP-framed.
BOOTROM_GREETING = b"\xc0\xc2\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xc0"

#: DTR drives RESET and RTS drives BOOT/IO16. These are the two pulse orders
#: kflash ships; on HUSKYLENS only the kd233 polarity enters the BootROM.
RESET_PROFILES = {
    "kd233": ((False, False), (True, False), (False, True)),
    "dan": ((False, False), (False, True), (True, False)),
}

REASONS = {
    0xE0: "OK",
    0xE1: "BAD_LENGTH",
    0xE2: "BAD_CHECKSUM",
    0xE3: "INVALID_COMMAND",
    0xE4: "BAD_INITIALIZATION",
    0xE5: "BAD_ADDRESS",
    0xE6: "FLASH_ERROR",
    0xE7: "BUSY",
}

#: The stub's per-request ceiling (ISP_MAX_BLOCK).
MAX_BLOCK = 4096


class IspError(RuntimeError):
    pass


def slip_encode(payload: bytes) -> bytes:
    escaped = payload.replace(SLIP_ESC, b"\xdb\xdd").replace(SLIP_END, b"\xdb\xdc")
    return SLIP_END + escaped + SLIP_END


def slip_decode(frame: bytes) -> bytes:
    return frame.replace(b"\xdb\xdc", SLIP_END).replace(b"\xdb\xdd", SLIP_ESC)


def build_request(
    op: int, address: int = 0, data: bytes = b"", length: int | None = None
) -> bytes:
    """Build an ISP request: op, reserved, crc32, address, length, data.

    ``length`` defaults to ``len(data)``. Reads set it independently, because a
    read request carries the number of bytes it wants but no data of its own.
    The CRC covers everything after the checksum field, which is what the
    stub's ``valid_payload`` recomputes.
    """
    body = struct.pack("<II", address, len(data) if length is None else length) + data
    checksum = binascii.crc32(body) & 0xFFFFFFFF
    return struct.pack("<HHI", op, 0, checksum) + body


class IspLink:
    def __init__(self, port: str, baudrate: int = 115200, timeout: float = 2.0):
        self.serial = serial.Serial()
        self.serial.port = port
        self.serial.baudrate = baudrate
        self.serial.timeout = timeout
        # The stub is already running; do not pulse the reset/boot lines.
        self.serial.dtr = False
        self.serial.rts = False
        self.serial.open()
        self.serial.dtr = False
        self.serial.rts = False

    def close(self) -> None:
        self.serial.close()

    def __enter__(self) -> "IspLink":
        return self

    def __exit__(self, *_exc: object) -> None:
        self.close()

    def _read_frame(self, deadline: float) -> bytes:
        buffer = bytearray()
        started = False
        while time.monotonic() < deadline:
            byte = self.serial.read(1)
            if not byte:
                continue
            if byte == SLIP_END:
                if not started:
                    started = True
                    continue
                if buffer:
                    return slip_decode(bytes(buffer))
                continue
            if started:
                buffer.extend(byte)
        raise IspError("timed out waiting for an ISP response")

    # -- BootROM stage ----------------------------------------------------

    def reset_to_isp(self, profile: str, pulse: float = 0.1) -> None:
        """Pulse RESET and BOOT so the mask ROM comes up in ISP mode."""
        for dtr, rts in RESET_PROFILES[profile]:
            self.serial.dtr = dtr
            self.serial.rts = rts
            time.sleep(pulse)
        # Leave both low so nothing resets the stub we are about to load.
        self.serial.dtr = False
        self.serial.rts = False
        self.serial.reset_input_buffer()

    def greet(self, attempts: int = 15) -> None:
        """Wait for the BootROM to answer a NOP."""
        for _ in range(attempts):
            self.serial.reset_input_buffer()
            self.serial.write(BOOTROM_GREETING)
            self.serial.flush()
            try:
                self._read_frame(time.monotonic() + 0.5)
                return
            except IspError:
                continue
        raise IspError("BootROM did not answer a greeting")

    def upload_sram(
        self, image: bytes, address: int = 0x80000000, chunk: int = 1024
    ) -> None:
        """Write an image into SRAM via ISP_MEMORY_WRITE."""
        for offset in range(0, len(image), chunk):
            piece = image[offset : offset + chunk]
            for attempt in range(8):
                try:
                    self._raw(ISP_MEMORY_WRITE, address + offset, piece, timeout=1.0)
                    break
                except IspError:
                    if attempt == 7:
                        raise
            if offset % (8 * 1024) == 0:
                print(
                    f"  uploading {offset + len(piece):>6}/{len(image)} bytes",
                    end="\r",
                    flush=True,
                )
        print(f"  uploaded {len(image)} bytes to SRAM        ")

    def switch_baud(self, baudrate: int) -> None:
        """Move both ends of the link to a new baud rate.

        The stub reconfigures its UART before sending the acknowledgement, so
        that acknowledgement arrives at the *new* rate and is unreadable at the
        old one. Fire and forget, re-rate the host, then confirm with a NOP.
        """
        self.serial.write(
            slip_encode(build_request(ISP_BAUDRATE, 0, struct.pack("<I", baudrate)))
        )
        self.serial.flush()
        time.sleep(0.2)
        self.serial.baudrate = baudrate
        time.sleep(0.2)
        self.serial.reset_input_buffer()
        for attempt in range(5):
            try:
                self._raw(0xD2, 0, b"", timeout=1.0)
                return
            except IspError:
                if attempt == 4:
                    raise IspError(
                        f"link did not come back at {baudrate} baud after the switch"
                    )

    def boot(self, address: int = 0x80000000) -> None:
        """Jump to the uploaded image. The BootROM does not acknowledge this."""
        body = struct.pack("<II", address, 0)
        checksum = binascii.crc32(body) & 0xFFFFFFFF
        self.serial.write(
            slip_encode(struct.pack("<HHI", ISP_MEMORY_BOOT, 0, checksum) + body)
        )
        self.serial.flush()

    # -- request plumbing --------------------------------------------------

    def _raw(
        self,
        op: int,
        address: int,
        data: bytes,
        timeout: float,
        length: int | None = None,
    ) -> bytes:
        """Send a request and return the raw response frame."""
        self.serial.reset_input_buffer()
        self.serial.write(slip_encode(build_request(op, address, data, length)))
        self.serial.flush()
        frame = self._read_frame(time.monotonic() + timeout)
        if len(frame) < 2:
            raise IspError(f"short response: {frame.hex(' ')}")
        if frame[1] != ISP_OK:
            raise IspError(
                f"op 0x{op:02x} rejected: {REASONS.get(frame[1], hex(frame[1]))}"
            )
        return frame

    def command(
        self,
        op: int,
        address: int = 0,
        data: bytes = b"",
        timeout: float = 3.0,
        length: int | None = None,
    ) -> bytes:
        """Send a stub command and return its verified payload, if any."""
        frame = self._raw(op, address, data, timeout, length)
        payload = frame[2:]
        if not payload:
            return b""
        if len(payload) < 4:
            raise IspError("payload too short to contain its CRC")
        expected = struct.unpack("<I", payload[:4])[0]
        body = payload[4:]
        actual = binascii.crc32(body) & 0xFFFFFFFF
        if actual != expected:
            raise IspError(
                f"payload CRC mismatch: device said 0x{expected:08x}, computed 0x{actual:08x}"
            )
        return body


def boot_stub(link: IspLink, stub: Path, profile: str) -> None:
    """Load the dumper stub into SRAM and boot it.

    This must happen on the same open port handle that the dump then uses. The
    stub lives in SRAM, so closing and reopening the port can pulse DTR, reset
    the K210, and silently discard it -- which looks exactly like a stub that
    refuses to answer.

    Nothing here writes flash.
    """
    image = stub.read_bytes()
    print(f"Entering BootROM via the '{profile}' reset sequence ...")
    link.reset_to_isp(profile)
    link.greet()
    print(f"BootROM responded. Uploading {stub.name} ({len(image)} bytes):")
    link.upload_sram(image)
    link.boot()
    time.sleep(0.5)
    print("Stub booted.\n")


def dump(
    link: IspLink,
    size: int,
    block: int,
    output: Path,
    flash_type: int,
) -> None:
    print(f"Initializing flash (type {flash_type}) ...")
    link.command(ISP_FLASH_INIT, address=flash_type)

    jedec = link.command(ISP_FLASH_JEDEC)
    capacity = 1 << jedec[2]
    print(
        f"JEDEC ID {jedec.hex(' ')} -> manufacturer 0x{jedec[0]:02x}, "
        f"capacity {capacity // (1024 * 1024)} MiB"
    )
    if size > capacity:
        print(
            f"NOTE: requested {size} bytes but the part holds only {capacity}; "
            "dumping the reported capacity."
        )
        size = capacity

    output.parent.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    with output.open("wb") as handle:
        for address in range(0, size, block):
            length = min(block, size - address)
            data = link.command(
                ISP_FLASH_READ, address=address, length=length, timeout=5.0
            )
            if len(data) != length:
                # The stub returns data_length bytes; request it explicitly.
                raise IspError(
                    f"at 0x{address:06x} expected {length} bytes, got {len(data)}"
                )
            handle.write(data)
            if address % (256 * 1024) == 0:
                done = address + length
                rate = done / max(time.monotonic() - started, 1e-6) / 1024
                percent = 100.0 * done / size
                print(
                    f"  0x{address:07x}  {percent:5.1f}%  {rate:6.1f} KiB/s",
                    flush=True,
                )
    elapsed = time.monotonic() - started
    print(f"\nWrote {size} bytes to {output} in {elapsed:.1f}s")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyUSB0")
    parser.add_argument("--output", type=Path, default=Path("artifacts/stock_flash.bin"))
    parser.add_argument("--stub", type=Path, default=DEFAULT_STUB)
    parser.add_argument(
        "--profile",
        default="kd233",
        choices=sorted(RESET_PROFILES),
        help="DTR/RTS pulse order used to enter the BootROM",
    )
    parser.add_argument("--boot-baud", type=int, default=115200)
    parser.add_argument(
        "--read-baud",
        type=int,
        default=2000000,
        help="baud to switch to for the bulk read; 0 disables the switch",
    )
    parser.add_argument("--size", type=lambda v: int(v, 0), default=16 * 1024 * 1024)
    parser.add_argument("--block", type=int, default=MAX_BLOCK)
    parser.add_argument("--flash-type", type=int, default=1, help="1=SPI0, 0=SPI3")
    parser.add_argument(
        "--skip-boot",
        action="store_true",
        help="assume the dumper stub is already running",
    )
    args = parser.parse_args()

    if args.block > MAX_BLOCK:
        parser.error(f"--block cannot exceed the stub's {MAX_BLOCK}-byte limit")
    if not args.stub.is_file() and not args.skip_boot:
        parser.error(f"stub not found: {args.stub} (build it first)")

    with IspLink(args.port, args.boot_baud) as link:
        if not args.skip_boot:
            boot_stub(link, args.stub, args.profile)
        if args.read_baud and args.read_baud != args.boot_baud:
            print(f"Switching link to {args.read_baud} baud ...")
            link.switch_baud(args.read_baud)
            print("  confirmed.\n")
        dump(link, args.size, args.block, args.output, args.flash_type)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
