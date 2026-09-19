from __future__ import annotations

import binascii
import struct
import unittest


END = 0xC0
ESC = 0xDB
ESC_END = 0xDC
ESC_ESC = 0xDD
OK, BAD_CRC, BAD_COMMAND, BUSY = 0xE0, 0xE2, 0xE3, 0xE7


def request(operation: int, payload: bytes = b"") -> bytes:
    body = struct.pack("<HHI", operation, 0, binascii.crc32(payload)) + payload
    escaped = bytearray([END])
    for byte in body:
        if byte == END:
            escaped.extend((ESC, ESC_END))
        elif byte == ESC:
            escaped.extend((ESC, ESC_ESC))
        else:
            escaped.append(byte)
    escaped.append(END)
    return bytes(escaped)


def decode_slip(packet: bytes) -> bytes:
    output = bytearray()
    escaped = False
    for byte in packet[1:-1]:
        if escaped:
            output.append(END if byte == ESC_END else ESC if byte == ESC_ESC else byte)
            escaped = False
        elif byte == ESC:
            escaped = True
        else:
            output.append(byte)
    return bytes(output)


class ProtocolHarness:
    """Host-side protocol oracle; no serial port or K210 is required."""

    def __init__(self) -> None:
        self.initialized = False
        self.erase_polls = 0
        self.baud = 115_200
        self.confirmed: dict[int, int] = {}

    def feed(self, packet: bytes) -> tuple[int, int] | None:
        frame = decode_slip(packet)
        operation, _reserved, checksum = struct.unpack_from("<HHI", frame)
        payload = frame[8:]
        if checksum != binascii.crc32(payload):
            return operation, BAD_CRC
        if operation == 0xD2:
            return operation, OK
        if operation == 0xD7:
            self.initialized = True
            return operation, OK
        if operation == 0xD6:
            _address, length, self.baud = struct.unpack("<III", payload)
            return operation, OK if length == 4 else BAD_CRC
        if operation == 0xD8:
            self.erase_polls = 2
            return operation, BUSY
        if operation == 0xD9:
            self.erase_polls -= 1
            return operation, BUSY if self.erase_polls else OK
        if operation == 0xD4:
            address, length = struct.unpack_from("<II", payload)
            self.confirmed[address] = max(length, self.confirmed.get(address, 0))
            return operation, OK
        if operation == 0xD5:
            # The stub must acknowledge D5 before resetting the SoC.
            return operation, OK
        return operation, BAD_COMMAND


class ProtocolHarnessTests(unittest.TestCase):
    def test_full_kflash_command_cycle(self) -> None:
        stub = ProtocolHarness()
        self.assertEqual(stub.feed(request(0xD2)), (0xD2, OK))
        self.assertEqual(stub.feed(request(0xD6, struct.pack("<III", 0, 4, 2_000_000))),
                         (0xD6, OK))
        self.assertEqual(stub.baud, 2_000_000)
        self.assertEqual(stub.feed(request(0xD7, struct.pack("<II", 0, 0))),
                         (0xD7, OK))
        self.assertEqual(stub.feed(request(0xD8, struct.pack("<II", 0, 8192))),
                         (0xD8, BUSY))
        self.assertEqual(stub.feed(request(0xD9)), (0xD9, BUSY))
        self.assertEqual(stub.feed(request(0xD9)), (0xD9, OK))
        block = bytes([0, *struct.pack("<I", 4096 - 37)]) + bytes(4091)
        write = struct.pack("<II", 0, len(block)) + block
        self.assertEqual(stub.feed(request(0xD4, write)), (0xD4, OK))
        self.assertEqual(stub.feed(request(0xD4, write)), (0xD4, OK))
        self.assertEqual(stub.confirmed, {0: 4096})
        self.assertEqual(stub.feed(request(0xD5)), (0xD5, OK))

    def test_crc_and_unknown_command_rejection(self) -> None:
        packet = bytearray(request(0xD4, struct.pack("<II", 0, 1) + b"x"))
        packet[-2] ^= 1
        self.assertEqual(ProtocolHarness().feed(bytes(packet)), (0xD4, BAD_CRC))
        self.assertEqual(ProtocolHarness().feed(request(0xDA)), (0xDA, BAD_COMMAND))

    def test_slip_escaping_round_trip(self) -> None:
        payload = bytes((END, ESC, 1, END))
        self.assertEqual(decode_slip(request(0xD2, payload))[8:], payload)


if __name__ == "__main__":
    unittest.main()
