from __future__ import annotations

import struct
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from hmpy_client import (
    CAP_BOOT_FLAGS,
    DEFAULT_CONNECT_TIMEOUT,
    HELLO_FIXED,
    STATUS_FIXED,
    HmpyClient,
    HmpyTimeout,
    open_serial_transport,
    pack_name,
    unpack_name,
)
from hmpy_protocol import Frame, FrameFlag, LINE_HANDSHAKE, LINE_READY, MessageType, crc32, decode_frame, encode_frame


class FakeDeviceTransport:
    def __init__(self) -> None:
        self.rx = bytearray()
        self.files: dict[str, bytes] = {}
        self.startup = ""
        self.upload: dict[str, object] | None = None
        self.next_upload = 1
        self.run_id = 0
        self.running = False
        self.closed = False
        self.boot_flags = 0

    def read(self, size: int = 1) -> bytes:
        if not self.rx:
            return b""
        result = bytes(self.rx[:size])
        del self.rx[:size]
        return result

    def write(self, data: bytes) -> int:
        if data == LINE_HANDSHAKE:
            self.rx.extend(LINE_READY)
            return len(data)
        frame = decode_frame(data)
        self.handle(frame)
        return len(data)

    def send(self, request: Frame, payload: bytes = b"", *, more: bool = False) -> None:
        flags = FrameFlag.RESPONSE | (FrameFlag.MORE if more else FrameFlag(0))
        self.rx.extend(encode_frame(Frame(request.type, flags, request.request_id, payload)))

    def event(self, message_type: MessageType, payload: bytes) -> None:
        self.rx.extend(encode_frame(Frame(message_type, payload=payload)))

    def handle(self, frame: Frame) -> None:
        if frame.type is MessageType.HELLO:
            fixed = HELLO_FIXED.pack(1, 5, int(self.running), self.boot_flags, 0xFF | CAP_BOOT_FLAGS, 1024, 1016, 63, 65535, 256 * 1024, 0x390000, 8192)
            self.send(
                frame,
                fixed + pack_name("0.3.0") + pack_name("huskylens-sen0305"),
            )
        elif frame.type is MessageType.LIST:
            for name, data in sorted(self.files.items()):
                self.send(frame, pack_name(name) + struct.pack("<I", len(data)), more=True)
            self.send(frame)
        elif frame.type is MessageType.STAT:
            name, end = unpack_name(frame.payload)
            assert end == len(frame.payload)
            data = self.files[name]
            self.send(frame, struct.pack("<IB", len(data), name == self.startup))
        elif frame.type is MessageType.READ:
            name, offset = unpack_name(frame.payload)
            start, length = struct.unpack_from("<II", frame.payload, offset)
            data = self.files[name]
            end = len(data) if length == 0 else min(len(data), start + length)
            if start == end:
                self.send(frame, struct.pack("<II", start, len(data)))
            while start < end:
                chunk = data[start : min(end, start + 512)]
                next_offset = start + len(chunk)
                self.send(frame, struct.pack("<II", start, len(data)) + chunk, more=next_offset < end)
                start = next_offset
        elif frame.type is MessageType.UPLOAD_BEGIN:
            name, offset = unpack_name(frame.payload)
            size, expected_crc = struct.unpack_from("<II", frame.payload, offset)
            upload_id = self.next_upload
            self.next_upload += 1
            self.upload = {"id": upload_id, "name": name, "size": size, "crc": expected_crc, "data": bytearray()}
            self.send(frame, struct.pack("<II", upload_id, 0))
        elif frame.type is MessageType.UPLOAD_CHUNK:
            upload_id, offset = struct.unpack_from("<II", frame.payload)
            assert self.upload and upload_id == self.upload["id"]
            data = self.upload["data"]
            assert isinstance(data, bytearray) and offset == len(data)
            data.extend(frame.payload[8:])
            self.send(frame, struct.pack("<II", upload_id, len(data)))
        elif frame.type is MessageType.UPLOAD_COMMIT:
            assert self.upload
            data = bytes(self.upload["data"])
            assert len(data) == self.upload["size"] and crc32(data) == self.upload["crc"]
            name = str(self.upload["name"])
            self.files[name] = data
            self.upload = None
            self.send(frame)
            self.event(MessageType.FILE_CHANGED, bytes((1,)) + pack_name(name))
        elif frame.type is MessageType.UPLOAD_ABORT:
            self.upload = None
            self.send(frame)
        elif frame.type is MessageType.SET_STARTUP:
            self.startup, _ = unpack_name(frame.payload)
            self.send(frame)
        elif frame.type is MessageType.DELETE:
            name, _ = unpack_name(frame.payload)
            del self.files[name]
            if self.startup == name:
                self.startup = ""
            self.send(frame)
        elif frame.type is MessageType.RUN:
            name, offset = unpack_name(frame.payload)
            assert name in self.files or (not name and self.startup in self.files)
            _limit = struct.unpack_from("<I", frame.payload, offset)[0]
            self.run_id += 1
            self.running = True
            self.send(frame, struct.pack("<I", self.run_id))
            self.event(MessageType.STATE, struct.pack("<BBIQ", 2, 0, self.run_id, 1000))
        elif frame.type is MessageType.STOP:
            self.running = False
            self.send(frame)
        elif frame.type is MessageType.STATUS:
            self.send(frame, STATUS_FIXED.pack(int(self.running) * 2, 0, 5, 0, self.run_id, 12, 0, 0, 10, 20, 30, 0x390000, 8192))
        elif frame.type is MessageType.PING:
            self.event(MessageType.STDOUT, struct.pack("<II", self.run_id, 0) + b"hello\n")
            self.send(frame, frame.payload)
        elif frame.type is MessageType.SESSION_CLOSE:
            self.send(frame)
            self.closed = True
        else:
            raise AssertionError(frame.type)


class FakeClock:
    def __init__(self) -> None:
        self.now = 0.0

    def monotonic(self) -> float:
        return self.now

    def sleep(self, seconds: float) -> None:
        self.now += max(0.0, seconds)


class DelayedBootTransport:
    """Drops handshakes until a simulated reset/boot delay has elapsed."""

    def __init__(self, clock: FakeClock, ready_at: float) -> None:
        self.clock = clock
        self.ready_at = ready_at
        self.rx = bytearray(b"stale pre-reset output\n")
        self.reset_count = 0
        self.handshake_times: list[float] = []
        self.ready_sent_at: float | None = None

    def reset_input_buffer(self) -> None:
        self.reset_count += 1
        self.rx.clear()

    def read(self, size: int = 1) -> bytes:
        if not self.rx:
            return b""
        result = bytes(self.rx[:size])
        del self.rx[:size]
        return result

    def write(self, data: bytes) -> int:
        if data != LINE_HANDSHAKE:
            raise AssertionError(data)
        self.handshake_times.append(self.clock.monotonic())
        if self.clock.monotonic() >= self.ready_at and self.ready_sent_at is None:
            self.ready_sent_at = self.clock.monotonic()
            self.rx.extend(LINE_READY)
        return len(data)


class ClosedSerialTransport:
    def __init__(self) -> None:
        self.port = None
        self.baudrate = None
        self.timeout = None
        self.write_timeout = None
        self.dtr = True
        self.rts = True
        self.open_snapshot: dict[str, object] | None = None
        self.closed = False

    def open(self) -> None:
        self.open_snapshot = {
            "port": self.port,
            "baudrate": self.baudrate,
            "timeout": self.timeout,
            "write_timeout": self.write_timeout,
            "dtr": self.dtr,
            "rts": self.rts,
        }

    def close(self) -> None:
        self.closed = True


class HmpyClientWorkflowTests(unittest.TestCase):
    def test_serial_transport_is_configured_with_dtr_rts_low_before_open(self) -> None:
        transport = ClosedSerialTransport()
        constructor_calls: list[tuple[object, ...]] = []

        def construct(*args, **kwargs):
            constructor_calls.append(args + tuple(kwargs.items()))
            return transport

        opened = open_serial_transport(
            SimpleNamespace(Serial=construct),
            "COM10",
            115200,
            timeout=0.05,
            write_timeout=2.0,
        )

        self.assertIs(opened, transport)
        self.assertEqual(constructor_calls, [()])
        self.assertEqual(
            transport.open_snapshot,
            {
                "port": "COM10",
                "baudrate": 115200,
                "timeout": 0.05,
                "write_timeout": 2.0,
                "dtr": False,
                "rts": False,
            },
        )

    def test_connect_timeout_survives_five_second_boot_and_stops_retrying_at_ready(self) -> None:
        clock = FakeClock()
        transport = DelayedBootTransport(clock, ready_at=5.0)
        client = HmpyClient(
            transport,
            timeout=0.2,
            connect_timeout=DEFAULT_CONNECT_TIMEOUT,
            handshake_interval=0.25,
        )

        with patch("hmpy_client.time.monotonic", clock.monotonic), patch(
            "hmpy_client.time.sleep", clock.sleep
        ):
            client.open()
            writes_at_ready = len(transport.handshake_times)
            client.open()

        self.assertTrue(client.active)
        self.assertEqual(transport.reset_count, 1)
        self.assertGreaterEqual(clock.now, 5.0)
        self.assertLess(clock.now, DEFAULT_CONNECT_TIMEOUT)
        self.assertGreater(writes_at_ready, 1)
        self.assertEqual(len(transport.handshake_times), writes_at_ready)
        self.assertEqual(transport.handshake_times[-1], transport.ready_sent_at)

    def test_line_handshake_retries_are_bounded_by_connect_timeout(self) -> None:
        clock = FakeClock()
        transport = DelayedBootTransport(clock, ready_at=10.0)
        client = HmpyClient(
            transport,
            timeout=0.1,
            connect_timeout=1.0,
            handshake_interval=0.25,
        )

        with patch("hmpy_client.time.monotonic", clock.monotonic), patch(
            "hmpy_client.time.sleep", clock.sleep
        ):
            with self.assertRaises(HmpyTimeout):
                client.open()

        self.assertFalse(client.active)
        self.assertGreater(len(transport.handshake_times), 1)
        self.assertGreaterEqual(clock.now, 1.0)
        self.assertLess(clock.now, 1.01)

    def test_complete_storage_runtime_workflow_and_interleaved_events(self) -> None:
        transport = FakeDeviceTransport()
        client = HmpyClient(transport, timeout=0.2)
        client.open()
        hello = client.hello()
        self.assertEqual(hello.protocol_version, 1)
        self.assertEqual(hello.boot_flags, 0)
        self.assertTrue(hello.capabilities & CAP_BOOT_FLAGS)
        self.assertEqual(hello.upload_chunk, 1016)
        self.assertEqual(hello.source_max, 65535)

        content = bytes(range(256)) * 13
        client.upload("main.py", content)
        self.assertEqual(transport.files["main.py"], content)
        self.assertTrue(any(event.type is MessageType.FILE_CHANGED for event in client.events))
        self.assertEqual(client.list_files()[0].name, "main.py")
        client.set_startup("main.py")
        self.assertTrue(client.stat("main.py").startup)
        self.assertEqual(client.read_file("main.py"), content)
        self.assertEqual(client.read_file("main.py", 100, 777), content[100:877])

        run_id = client.run("main.py", 30_000)
        self.assertEqual(run_id, 1)
        self.assertEqual(client.status().run_id, 1)
        self.assertEqual(client.ping(b"lease"), b"lease")
        self.assertTrue(any(event.type is MessageType.STDOUT and event.data == b"hello\n" for event in client.events))
        client.stop()
        client.delete("main.py")
        self.assertEqual(client.list_files(), [])
        client.close()
        self.assertTrue(transport.closed)

    def test_name_codec_rejects_invalid_lengths(self) -> None:
        with self.assertRaises(ValueError):
            pack_name("")
        with self.assertRaises(ValueError):
            pack_name("x" * 64)

    def test_hello_exposes_backward_compatible_boot_flags(self) -> None:
        transport = FakeDeviceTransport()
        transport.boot_flags = 1
        client = HmpyClient(transport, timeout=0.2)
        client.open()
        self.assertEqual(client.hello().boot_flags, 1)
        client.close()


if __name__ == "__main__":
    unittest.main()
