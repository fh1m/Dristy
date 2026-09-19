"""Codec tests built from the worked examples in the v0.5.1 protocol document.

Every expected byte string below is copied from
https://github.com/Dristy/SEN0305Arduino/blob/master/SEN0305%20Protocol.md
so a passing run means we agree with DFRobot byte for byte, including their
published checksums.
"""

import unittest

from dlp_proto import Command, encode, iter_frames
from dlp_proto.frames import FrameError, decode_one
from dlp_proto.types import Arrow, Block, Info


class TestEncoding(unittest.TestCase):
    def test_documented_zero_argument_frames(self):
        cases = {
            Command.REQUEST: "55 aa 11 00 20 30",
            Command.REQUEST_BLOCKS: "55 aa 11 00 21 31",
            Command.REQUEST_ARROWS: "55 aa 11 00 22 32",
            Command.REQUEST_LEARNED: "55 aa 11 00 23 33",
            Command.REQUEST_BLOCKS_LEARNED: "55 aa 11 00 24 34",
            Command.REQUEST_ARROWS_LEARNED: "55 aa 11 00 25 35",
            Command.REQUEST_KNOCK: "55 aa 11 00 2c 3c",
            Command.RETURN_OK: "55 aa 11 00 2e 3e",
            Command.REQUEST_PHOTO: "55 aa 11 00 30 40",
            Command.REQUEST_CLEAR_TEXT: "55 aa 11 00 35 45",
            Command.REQUEST_FORGET: "55 aa 11 00 37 47",
            Command.REQUEST_SAVE_SCREENSHOT: "55 aa 11 00 39 49",
            Command.REQUEST_IS_PRO: "55 aa 11 00 3b 4b",
            Command.RETURN_BUSY: "55 aa 11 00 3d 4d",
            Command.RETURN_NEED_PRO: "55 aa 11 00 3e 4e",
        }
        for command, expected in cases.items():
            with self.subTest(command=command.name):
                self.assertEqual(encode(command).hex(" "), expected)

    def test_documented_id_argument_frames(self):
        cases = {
            Command.REQUEST_BY_ID: "55 aa 11 02 26 01 00 39",
            Command.REQUEST_BLOCKS_BY_ID: "55 aa 11 02 27 01 00 3a",
            Command.REQUEST_ARROWS_BY_ID: "55 aa 11 02 28 01 00 3b",
            Command.REQUEST_ALGORITHM: "55 aa 11 02 2d 01 00 40",
            Command.REQUEST_LEARN: "55 aa 11 02 36 01 00 49",
        }
        for command, expected in cases.items():
            with self.subTest(command=command.name):
                self.assertEqual(encode(command, b"\x01\x00").hex(" "), expected)

    def test_documented_return_block_checksum(self):
        # The document works this checksum out by hand as the low byte of 0x258.
        payload = bytes.fromhex("2c01c8000a00140001 00".replace(" ", ""))
        self.assertEqual(
            encode(Command.RETURN_BLOCK, payload).hex(" "),
            "55 aa 11 0a 2a 2c 01 c8 00 0a 00 14 00 01 00 58",
        )

    def test_documented_return_info_frame(self):
        payload = bytes.fromhex("01000100050000000000")
        self.assertEqual(
            encode(Command.RETURN_INFO, payload).hex(" "),
            "55 aa 11 0a 29 01 00 01 00 05 00 00 00 00 00 4a",
        )

    def test_documented_custom_text_frame(self):
        payload = bytes([0x06, 0x00, 0x78, 0x78]) + b"TEST_1"
        self.assertEqual(
            encode(Command.REQUEST_CUSTOM_TEXT, payload).hex(" "),
            "55 aa 11 0a 34 06 00 78 78 54 45 53 54 5f 31 14",
        )

    def test_documented_custom_name_frame(self):
        payload = bytes([0x01, 0x05]) + b"TEST" + b"\x00"
        self.assertEqual(
            encode(Command.REQUEST_CUSTOMNAMES, payload).hex(" "),
            "55 aa 11 07 2f 01 05 54 45 53 54 00 8c",
        )


class TestDecoding(unittest.TestCase):
    def test_roundtrip(self):
        raw = encode(Command.RETURN_BLOCK, bytes(range(10)))
        frame, end = decode_one(raw)
        self.assertEqual(end, len(raw))
        self.assertEqual(frame.command, Command.RETURN_BLOCK)
        self.assertEqual(frame.data, bytes(range(10)))

    def test_resyncs_past_leading_garbage(self):
        raw = b"\x00\xff\x55\x13" + encode(Command.RETURN_OK)
        frame, _end = decode_one(raw)
        self.assertEqual(frame.command, Command.RETURN_OK)

    def test_rejects_bad_checksum(self):
        raw = bytearray(encode(Command.RETURN_OK))
        raw[-1] ^= 0xFF
        with self.assertRaises(FrameError):
            decode_one(bytes(raw))

    def test_incomplete_frame_yields_nothing(self):
        raw = encode(Command.RETURN_BLOCK, bytes(range(10)))[:-3]
        self.assertEqual(list(iter_frames(raw)), [])

    def test_parses_a_full_response_batch(self):
        stream = (
            encode(Command.RETURN_INFO, bytes.fromhex("02000100050000000000"))
            + encode(Command.RETURN_BLOCK, bytes.fromhex("2c01c8000a00140001 00".replace(" ", "")))
            + encode(Command.RETURN_ARROW, bytes.fromhex("2c01c8000a00140002 00".replace(" ", "")))
        )
        parsed = [frame for frame, _s, _e in iter_frames(stream)]
        self.assertEqual(len(parsed), 3)

        info = Info.decode(parsed[0].data)
        self.assertEqual((info.result_count, info.learned_count, info.frame_number), (2, 1, 5))

        block = Block.decode(parsed[1].data)
        self.assertEqual(
            (block.x_center, block.y_center, block.width, block.height, block.object_id),
            (300, 200, 10, 20, 1),
        )
        self.assertEqual(block.corners, (295, 190, 305, 210))
        self.assertTrue(block.is_learned)

        arrow = Arrow.decode(parsed[2].data)
        self.assertEqual(
            (arrow.x_origin, arrow.y_origin, arrow.x_target, arrow.y_target, arrow.object_id),
            (300, 200, 10, 20, 2),
        )


if __name__ == "__main__":
    unittest.main()
