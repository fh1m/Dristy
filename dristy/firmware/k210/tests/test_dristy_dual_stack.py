import os
import sys
import unittest
from pathlib import Path

HUSKY = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(HUSKY / "dristy-py"))

from dristy.protocol import CMD_IDENTIFY, build_packet, parse_packet


class DristyDualStackTests(unittest.TestCase):
    def test_dlp_stream_recognised_before_hk_parser(self) -> None:
        """DLP uses 0x55 0xAA 0x11 addr framing (same as Dristy)."""
        pkt = build_packet(CMD_IDENTIFY, b"")
        buf = bytearray()
        for byte in pkt:
            buf.append(byte)
            hit = parse_packet(buf)
            if hit:
                cmd, _, consumed = hit
                self.assertEqual(cmd, CMD_IDENTIFY)
                self.assertEqual(consumed, len(pkt))
                return
        self.fail("DLP packet not parsed")

    def test_hk_and_dlp_share_preamble(self) -> None:
        """Both stacks use 0x55 0xAA — bridge must demux by command range."""
        dlp = build_packet(0x4F, b"")  # IDENTIFY
        self.assertEqual(dlp[0:2], b"\x55\xaa")
        hk_knock = build_packet(0x2C, b"")  # stock KNOCK
        self.assertEqual(hk_knock[0:2], b"\x55\xaa")
        self.assertNotEqual(dlp[4], hk_knock[4])


if __name__ == "__main__":
    unittest.main()
