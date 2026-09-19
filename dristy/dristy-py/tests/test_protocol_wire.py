"""DLP packet round-trip and wire size contracts."""

import struct
import unittest

from dristy.protocol import (
    CMD_IDENTIFY,
    CMD_GET_TRACKS,
    build_packet,
    parse_packet,
)


class ProtocolWireTests(unittest.TestCase):
    def test_identify_packet_framing(self) -> None:
        pkt = build_packet(CMD_IDENTIFY, b"")
        self.assertEqual(pkt[:2], b"\x55\xaa")
        parsed = parse_packet(bytearray(pkt))
        self.assertIsNotNone(parsed)
        cmd, data, consumed = parsed
        self.assertEqual(cmd, CMD_IDENTIFY)
        self.assertEqual(data, b"")
        self.assertEqual(consumed, len(pkt))

    def test_track_record_size_matches_firmware(self) -> None:
        """Firmware packs dlp_track_wire_t as 16 bytes (see dristy_protocol.h)."""
        sample = struct.pack("<HhhhhhhH", 1, 10, 20, 30, 40, 5, -3, 7)
        self.assertEqual(len(sample), 16)
        pkt = build_packet(CMD_GET_TRACKS, sample)
        parsed = parse_packet(bytearray(pkt))
        self.assertIsNotNone(parsed)
        _, data, _ = parsed
        self.assertEqual(len(data), 16)


if __name__ == "__main__":
    unittest.main()
