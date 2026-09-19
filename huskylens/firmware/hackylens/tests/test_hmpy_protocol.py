from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import hmpy_protocol as hmpy  # noqa: E402


FIXTURE_PATH = ROOT / "tests" / "fixtures" / "hmpy_golden.json"


C_HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hmpy_codec.h"

static int hex_digit(char value)
{
    if(value >= '0' && value <= '9') return value - '0';
    if(value >= 'a' && value <= 'f') return value - 'a' + 10;
    if(value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static size_t decode_hex(const char *text, uint8_t *output, size_t capacity)
{
    size_t length = strlen(text);
    size_t index;
    if((length & 1U) || length / 2U > capacity) return (size_t)-1;
    for(index = 0U; index < length / 2U; index++)
    {
        int high = hex_digit(text[index * 2U]);
        int low = hex_digit(text[index * 2U + 1U]);
        if(high < 0 || low < 0) return (size_t)-1;
        output[index] = (uint8_t)((high << 4) | low);
    }
    return length / 2U;
}

static void print_hex(const uint8_t *data, size_t length)
{
    size_t index;
    for(index = 0U; index < length; index++) printf("%02x", data[index]);
}

static int feed_wire(hmpy_stream_decoder_t *decoder, const uint8_t *wire, size_t length,
                     hmpy_frame_t *frame, hmpy_codec_status_t *last)
{
    size_t index;
    *last = HMPY_CODEC_INCOMPLETE;
    for(index = 0U; index < length; index++)
    {
        *last = hmpy_stream_decoder_feed(decoder, wire[index], frame);
        if(index + 1U < length && *last != HMPY_CODEC_INCOMPLETE) return 0;
    }
    return 1;
}

static int selftest(void)
{
    uint8_t payload[HMPY_MAX_PAYLOAD_SIZE];
    uint8_t wire[HMPY_MAX_WIRE_FRAME];
    uint8_t damaged[HMPY_MAX_WIRE_FRAME];
    uint8_t raw[HMPY_MAX_RAW_FRAME];
    const uint8_t zero_payload[] = {0U, 1U, 0U, 0xFFU, 0U};
    const uint8_t malformed[] = {5U, 'a', 'b', 'c', 0U};
    hmpy_stream_decoder_t decoder;
    hmpy_frame_t frame;
    hmpy_codec_status_t status;
    size_t wire_length = 0U;
    size_t raw_length = 0U;
    size_t damaged_length = 0U;
    size_t index;

    hmpy_stream_decoder_init(&decoder);
    status = hmpy_frame_encode(HMPY_MSG_UPLOAD_CHUNK, 0U, 0x12345678UL,
                               zero_payload, sizeof(zero_payload), wire, sizeof(wire),
                               &wire_length);
    if(status != HMPY_CODEC_OK) return 10;
    if(!feed_wire(&decoder, wire, wire_length, &frame, &status)) return 11;
    if(status != HMPY_CODEC_FRAME_READY || frame.type != HMPY_MSG_UPLOAD_CHUNK ||
       frame.flags != 0U || frame.request_id != 0x12345678UL ||
       frame.payload_length != sizeof(zero_payload) ||
       memcmp(frame.payload, zero_payload, sizeof(zero_payload)) != 0) return 12;

    status = hmpy_cobs_decode(wire, wire_length - 1U, raw, sizeof(raw), &raw_length);
    if(status != HMPY_CODEC_OK || raw_length <= HMPY_HEADER_SIZE) return 13;
    raw[HMPY_HEADER_SIZE] ^= 0x80U;
    status = hmpy_cobs_encode(raw, raw_length, damaged, sizeof(damaged) - 1U,
                              &damaged_length);
    if(status != HMPY_CODEC_OK) return 14;
    damaged[damaged_length++] = 0U;
    if(!feed_wire(&decoder, damaged, damaged_length, &frame, &status)) return 15;
    if(status != HMPY_CODEC_ERROR_CRC_MISMATCH) return 16;

    if(!feed_wire(&decoder, malformed, sizeof(malformed), &frame, &status)) return 17;
    if(status != HMPY_CODEC_ERROR_COBS_MALFORMED) return 18;
    if(!feed_wire(&decoder, wire, wire_length, &frame, &status)) return 19;
    if(status != HMPY_CODEC_FRAME_READY) return 20;

    for(index = 0U; index < HMPY_MAX_ENCODED_PACKET + 1U; index++)
    {
        status = hmpy_stream_decoder_feed(&decoder, 1U, &frame);
        if(status != HMPY_CODEC_INCOMPLETE) return 21;
    }
    status = hmpy_stream_decoder_feed(&decoder, 0U, &frame);
    if(status != HMPY_CODEC_ERROR_ENCODED_TOO_LARGE) return 22;
    if(!feed_wire(&decoder, wire, wire_length, &frame, &status)) return 23;
    if(status != HMPY_CODEC_FRAME_READY) return 24;

    memset(raw, 0, sizeof(raw));
    status = hmpy_cobs_encode(raw, sizeof(raw), damaged, sizeof(damaged) - 1U,
                              &damaged_length);
    if(status != HMPY_CODEC_OK) return 25;
    /* One more decoded zero still fits the encoded-packet limit, but not the
       fixed decoded-frame limit. */
    if(damaged_length >= sizeof(damaged) - 1U) return 26;
    damaged[damaged_length++] = 1U;
    damaged[damaged_length++] = 0U;
    if(!feed_wire(&decoder, damaged, damaged_length, &frame, &status)) return 27;
    if(status != HMPY_CODEC_ERROR_ENCODED_TOO_LARGE) return 28;

    for(index = 0U; index < sizeof(payload); index++) payload[index] = (uint8_t)index;
    status = hmpy_frame_encode(HMPY_MSG_PING, 0U, 99U, payload, sizeof(payload),
                               wire, sizeof(wire), &wire_length);
    if(status != HMPY_CODEC_OK || wire_length > HMPY_MAX_WIRE_FRAME) return 29;
    if(!feed_wire(&decoder, wire, wire_length, &frame, &status)) return 30;
    if(status != HMPY_CODEC_FRAME_READY || frame.payload_length != sizeof(payload) ||
       memcmp(frame.payload, payload, sizeof(payload)) != 0) return 31;
    if(decoder.error_count != 4U) return 32;
    return 0;
}

int main(int argc, char **argv)
{
    uint8_t payload[HMPY_MAX_PAYLOAD_SIZE];
    uint8_t wire[HMPY_MAX_WIRE_FRAME];
    hmpy_stream_decoder_t decoder;
    hmpy_frame_t frame;
    hmpy_codec_status_t status;
    size_t payload_length;
    size_t wire_length = 0U;

    if(argc == 2 && strcmp(argv[1], "selftest") == 0)
    {
        int result = selftest();
        if(result != 0)
        {
            fprintf(stderr, "selftest:%d\n", result);
            return result;
        }
        puts("ok");
        return 0;
    }
    if(argc == 6 && strcmp(argv[1], "encode") == 0)
    {
        unsigned long type = strtoul(argv[2], NULL, 0);
        unsigned long flags = strtoul(argv[3], NULL, 0);
        unsigned long request_id = strtoul(argv[4], NULL, 0);
        payload_length = decode_hex(argv[5], payload, sizeof(payload));
        if(payload_length == (size_t)-1) return 2;
        status = hmpy_frame_encode((uint8_t)type, (uint16_t)flags, (uint32_t)request_id,
                                   payload, (uint32_t)payload_length, wire, sizeof(wire),
                                   &wire_length);
        if(status != HMPY_CODEC_OK)
        {
            fprintf(stderr, "codec:%d\n", (int)status);
            return 3;
        }
        print_hex(wire, wire_length);
        putchar('\n');
        return 0;
    }
    if(argc == 3 && strcmp(argv[1], "decode") == 0)
    {
        wire_length = decode_hex(argv[2], wire, sizeof(wire));
        if(wire_length == (size_t)-1) return 2;
        hmpy_stream_decoder_init(&decoder);
        if(!feed_wire(&decoder, wire, wire_length, &frame, &status) ||
           status != HMPY_CODEC_FRAME_READY)
        {
            fprintf(stderr, "codec:%d\n", (int)status);
            return 3;
        }
        printf("%u %u %lu ", (unsigned)frame.type, (unsigned)frame.flags,
               (unsigned long)frame.request_id);
        print_hex(frame.payload, frame.payload_length);
        putchar('\n');
        return 0;
    }
    fprintf(stderr, "usage: hmpy_harness selftest|encode TYPE FLAGS ID HEX|decode HEX\n");
    return 2;
}
"""


class GoldenVectorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.fixture = json.loads(FIXTURE_PATH.read_text(encoding="utf-8"))["vectors"]

    def _frame(self, vector: dict[str, object]) -> hmpy.Frame:
        return hmpy.Frame(
            hmpy.MessageType(int(vector["type"])),
            hmpy.FrameFlag(int(vector["flags"])),
            int(vector["request_id"]),
            bytes.fromhex(str(vector["payload_hex"])),
        )

    def test_python_matches_golden_raw_and_wire(self) -> None:
        for vector in self.fixture:
            with self.subTest(vector=vector["name"]):
                frame = self._frame(vector)
                raw = hmpy.encode_raw(frame)
                self.assertEqual(raw.hex(), vector["raw_hex"])
                self.assertEqual(hmpy.encode_frame(frame).hex(), vector["wire_hex"])
                self.assertEqual(f"{hmpy.crc32(raw[:-4]):08x}", vector["crc32"])

    def test_python_decodes_golden_wire(self) -> None:
        for vector in self.fixture:
            with self.subTest(vector=vector["name"]):
                self.assertEqual(
                    hmpy.decode_frame(bytes.fromhex(str(vector["wire_hex"]))),
                    self._frame(vector),
                )


class PythonRobustnessTests(unittest.TestCase):
    def test_fragmentation_and_multiple_frames(self) -> None:
        expected = [
            hmpy.Frame(hmpy.MessageType.HELLO, request_id=7),
            hmpy.Frame(hmpy.MessageType.STDOUT, payload=b"zero\0inside"),
        ]
        wire = b"".join(hmpy.encode_frame(frame) for frame in expected)
        decoder = hmpy.StreamDecoder()
        events: list[hmpy.StreamEvent] = []
        offset = 0
        for size in (1, 2, 7, 3, 11, 5, 1000):
            events.extend(decoder.feed(wire[offset : offset + size]))
            offset += size
            if offset >= len(wire):
                break
        self.assertEqual([event.frame for event in events if event.ok], expected)
        self.assertFalse([event for event in events if not event.ok])

    def test_noise_malformed_cobs_and_crc_recover_at_delimiter(self) -> None:
        good_frame = hmpy.Frame(hmpy.MessageType.PING, request_id=9, payload=b"ok")
        good_wire = hmpy.encode_frame(good_frame)
        raw = bytearray(hmpy.encode_raw(good_frame))
        raw[hmpy.HEADER_SIZE] ^= 0x40
        bad_crc_wire = hmpy.cobs_encode(raw) + b"\0"
        malformed = b"\x05abc\0"

        decoder = hmpy.StreamDecoder()
        events = decoder.feed(malformed + bad_crc_wire + good_wire)
        self.assertEqual(
            [event.error.status for event in events if event.error],
            [
                hmpy.CodecStatus.ERROR_COBS_MALFORMED,
                hmpy.CodecStatus.ERROR_CRC_MISMATCH,
            ],
        )
        self.assertEqual([event.frame for event in events if event.frame], [good_frame])
        self.assertEqual(decoder.error_count, 2)

    def test_oversize_packet_is_bounded_and_recovers(self) -> None:
        good_frame = hmpy.Frame(hmpy.MessageType.STATUS, request_id=10)
        decoder = hmpy.StreamDecoder()
        events = decoder.feed(
            b"\x01" * (hmpy.MAX_ENCODED_PACKET + 1)
            + b"\0"
            + hmpy.encode_frame(good_frame)
        )
        self.assertEqual(events[0].error.status, hmpy.CodecStatus.ERROR_ENCODED_TOO_LARGE)
        self.assertEqual(events[1].frame, good_frame)

    def test_decoded_oversize_packet_is_rejected(self) -> None:
        packet = hmpy.cobs_encode(b"\0" * (hmpy.MAX_RAW_FRAME + 1))
        self.assertLessEqual(len(packet), hmpy.MAX_ENCODED_PACKET)
        with self.assertRaises(hmpy.ProtocolDecodeError) as caught:
            hmpy.decode_packet(packet)
        self.assertEqual(caught.exception.status, hmpy.CodecStatus.ERROR_ENCODED_TOO_LARGE)

    def test_embedded_zero_and_maximum_payload(self) -> None:
        payload = bytes(range(256)) * 4
        frame = hmpy.Frame(hmpy.MessageType.PING, request_id=11, payload=payload)
        wire = hmpy.encode_frame(frame)
        self.assertLessEqual(len(wire), hmpy.MAX_WIRE_FRAME)
        self.assertNotIn(0, wire[:-1])
        self.assertEqual(hmpy.decode_frame(wire), frame)

    def test_rejects_version_flags_envelope_and_length(self) -> None:
        frame = hmpy.Frame(hmpy.MessageType.HELLO, request_id=1)
        raw = bytearray(hmpy.encode_raw(frame))

        wrong_version = raw.copy()
        wrong_version[4] = 2
        with self.assertRaisesRegex(hmpy.ProtocolDecodeError, "unsupported-version") as caught:
            hmpy.decode_raw(wrong_version)
        self.assertEqual(caught.exception.status, hmpy.CodecStatus.ERROR_UNSUPPORTED_VERSION)

        reserved_flags = raw.copy()
        reserved_flags[6:8] = (0x8000).to_bytes(2, "little")
        with self.assertRaises(hmpy.ProtocolDecodeError) as caught:
            hmpy.decode_raw(reserved_flags)
        self.assertEqual(caught.exception.status, hmpy.CodecStatus.ERROR_RESERVED_FLAGS)

        with self.assertRaises(hmpy.ProtocolDecodeError) as caught:
            hmpy.encode_frame(hmpy.Frame(hmpy.MessageType.HELLO, request_id=0))
        self.assertEqual(caught.exception.status, hmpy.CodecStatus.ERROR_INVALID_ENVELOPE)

        wrong_length = raw + b"extra"
        with self.assertRaises(hmpy.ProtocolDecodeError) as caught:
            hmpy.decode_raw(wrong_length)
        self.assertEqual(caught.exception.status, hmpy.CodecStatus.ERROR_LENGTH_MISMATCH)

    def test_payload_over_limit_is_rejected(self) -> None:
        with self.assertRaises(hmpy.ProtocolDecodeError) as caught:
            hmpy.encode_frame(
                hmpy.Frame(
                    hmpy.MessageType.PING,
                    request_id=12,
                    payload=b"x" * (hmpy.MAX_PAYLOAD_SIZE + 1),
                )
            )
        self.assertEqual(caught.exception.status, hmpy.CodecStatus.ERROR_PAYLOAD_TOO_LARGE)

    def test_packet_api_rejects_embedded_delimiter(self) -> None:
        with self.assertRaises(hmpy.ProtocolDecodeError) as caught:
            hmpy.decode_packet(b"\x02\x00")
        self.assertEqual(caught.exception.status, hmpy.CodecStatus.ERROR_COBS_MALFORMED)


class CInteropTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        compiler = next(
            (
                path
                for name in (os.environ.get("CC"), "cc", "gcc", "clang")
                if name and (path := shutil.which(name))
            ),
            None,
        )
        if compiler is None:
            raise unittest.SkipTest("no host C compiler available")

        cls.fixture = json.loads(FIXTURE_PATH.read_text(encoding="utf-8"))["vectors"]
        cls._temp = tempfile.TemporaryDirectory(prefix="hmpy-codec-")
        temp_path = Path(cls._temp.name)
        source = temp_path / "hmpy_harness.c"
        source.write_text(C_HARNESS, encoding="utf-8")
        cls.executable = temp_path / ("hmpy_harness.exe" if os.name == "nt" else "hmpy_harness")
        subprocess.run(
            [
                compiler,
                "-std=c99",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(ROOT / "firmware" / "src" / "services"),
                str(ROOT / "firmware" / "src" / "services" / "hmpy_codec.c"),
                str(source),
                "-o",
                str(cls.executable),
            ],
            check=True,
            capture_output=True,
            text=True,
        )

    @classmethod
    def tearDownClass(cls) -> None:
        if hasattr(cls, "_temp"):
            cls._temp.cleanup()

    def _run(self, *arguments: str) -> str:
        result = subprocess.run(
            [str(self.executable), *arguments],
            check=True,
            capture_output=True,
            text=True,
        )
        return result.stdout.strip()

    def test_c_streaming_robustness_selftest(self) -> None:
        self.assertEqual(self._run("selftest"), "ok")

    def test_c_encoding_matches_golden_vectors(self) -> None:
        for vector in self.fixture:
            with self.subTest(vector=vector["name"]):
                actual = self._run(
                    "encode",
                    str(vector["type"]),
                    str(vector["flags"]),
                    str(vector["request_id"]),
                    str(vector["payload_hex"]),
                )
                self.assertEqual(actual, vector["wire_hex"])

    def test_c_decoding_matches_golden_vectors(self) -> None:
        for vector in self.fixture:
            with self.subTest(vector=vector["name"]):
                output = self._run("decode", str(vector["wire_hex"]))
                fields = output.split(" ", 3)
                message_type, flags, request_id = fields[:3]
                payload_hex = fields[3] if len(fields) == 4 else ""
                self.assertEqual(int(message_type), vector["type"])
                self.assertEqual(int(flags), vector["flags"])
                self.assertEqual(int(request_id), vector["request_id"])
                self.assertEqual(payload_hex, vector["payload_hex"])


if __name__ == "__main__":
    unittest.main()
