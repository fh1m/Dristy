"""Serial transport for the SEN0305.

On this board the CP2102N modem control lines are not flow control: DTR drives
the K210 RESET pin and RTS drives BOOT/IO16. Asserting them in the wrong order
is exactly the sequence a flashing tool uses to drop the chip into its ISP
bootloader. Both lines are therefore held low for the entire session, and they
are configured before ``open()`` because many Linux serial drivers assert DTR
as a side effect of opening the port.
"""

from __future__ import annotations

import time
from typing import Callable, Iterator, Optional

import serial
from serial.tools import list_ports

from . import frames
from .frames import Frame

#: USB vendor IDs used by the USB-UART bridges found on SEN0305 boards.
#: 0x10C4 is the CP210x family, which is what SEN0305 ships with.
KNOWN_VIDS = (0x10C4, 0x1A86, 0x0403, 0x067B)

#: Baud rates worth trying when the configured rate is unknown. The device menu
#: offers 9600, 115200 and 1000000; DFRobot's own Python library defaults to
#: 3000000 over USB, which the menu never exposes.
CANDIDATE_BAUDS = (9600, 115200, 1000000, 3000000, 2000000, 230400, 57600)


class TransportError(IOError):
    """Raised when the serial link cannot be established or used."""


def find_ports() -> list[str]:
    """Return device paths for every attached SEN0305-compatible bridge."""
    return [p.device for p in list_ports.comports() if p.vid in KNOWN_VIDS]


class SerialTransport:
    """A framed, resynchronising serial link to the SEN0305."""

    def __init__(
        self,
        port: str = "/dev/ttyUSB0",
        baudrate: int = 9600,
        timeout: float = 0.5,
        settle: float = 0.25,
        warmup: bool = True,
    ) -> None:
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.settle = settle
        self.warmup = warmup
        self._serial: Optional[serial.Serial] = None
        self._buffer = bytearray()

    def open(self) -> "SerialTransport":
        if self._serial is not None:
            return self
        handle = serial.Serial()
        handle.port = self.port
        handle.baudrate = self.baudrate
        handle.timeout = self.timeout
        handle.dtr = False
        handle.rts = False
        try:
            handle.open()
        except serial.SerialException as exc:
            raise TransportError(f"cannot open {self.port}: {exc}") from exc
        # Re-assert after open; pyserial may raise DTR while opening.
        handle.dtr = False
        handle.rts = False
        self._serial = handle
        time.sleep(self.settle)
        self.flush()
        if self.warmup:
            self._warm_up()
        return self

    def _warm_up(self) -> None:
        """Absorb the first-command drop.

        Measured on SEN0305 firmware: the first command sent after the port is
        opened is discarded every time, across every session tested. Sending a
        throwaway KNOCK here means callers never see the phantom timeout that
        this otherwise causes on their first real request.
        """
        from .commands import Command  # local import avoids a cycle

        for _ in range(3):
            if self.exchange(Command.REQUEST_KNOCK, timeout=0.3):
                break
        time.sleep(0.05)
        self.flush()

    def close(self) -> None:
        if self._serial is not None:
            self._serial.close()
            self._serial = None
        self._buffer.clear()

    def __enter__(self) -> "SerialTransport":
        return self.open()

    def __exit__(self, *_exc_info: object) -> None:
        self.close()

    @property
    def handle(self) -> serial.Serial:
        if self._serial is None:
            raise TransportError("transport is not open")
        return self._serial

    def flush(self) -> None:
        self.handle.reset_input_buffer()
        self.handle.reset_output_buffer()
        self._buffer.clear()

    def send(self, command: int, data: bytes = b"") -> bytes:
        payload = frames.encode(command, data)
        self.handle.write(payload)
        self.handle.flush()
        return payload

    def read_frames(self, deadline: float) -> Iterator[Frame]:
        """Yield frames until ``deadline`` (an absolute ``time.monotonic``)."""
        while True:
            for frame, _start, end in frames.iter_frames(bytes(self._buffer)):
                del self._buffer[:end]
                yield frame
                break
            else:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return
                self.handle.timeout = min(remaining, self.timeout)
                chunk = self.handle.read(max(1, self.handle.in_waiting or 1))
                if chunk:
                    self._buffer.extend(chunk)

    def exchange(
        self,
        command: int,
        data: bytes = b"",
        timeout: Optional[float] = None,
        stop_when: Optional[Callable[[list[Frame]], bool]] = None,
    ) -> list[Frame]:
        """Send one command and collect the frames it produces.

        Without ``stop_when`` this waits out the whole timeout, because the
        protocol gives no way to know a batch has ended. Supplying a predicate
        lets a caller return as soon as the response is complete, which is the
        difference between a timeout-bound poll rate and a real one.
        """
        self.send(command, data)
        deadline = time.monotonic() + (timeout if timeout is not None else self.timeout)
        collected: list[Frame] = []
        for frame in self.read_frames(deadline):
            collected.append(frame)
            if stop_when is not None and stop_when(collected):
                break
        return collected
