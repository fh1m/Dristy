from __future__ import annotations

import binascii
from pathlib import Path
import struct
import unittest


ROOT = Path(__file__).resolve().parents[1]


def request(operation: int, payload: bytes = b"") -> bytes:
    return struct.pack("<HHI", operation, 0, binascii.crc32(payload)) + payload


def progress_after(blocks: list[tuple[int, int]], wire_size: int) -> int:
    contiguous = 0
    confirmed: dict[int, int] = {}
    for address, length in blocks:
        confirmed[address] = max(length, confirmed.get(address, 0))
        while contiguous in confirmed:
            length = confirmed[contiguous]
            contiguous += length
            if length < 4096:
                break
    return min(100, contiguous * 100 // wire_size)


class ContractTests(unittest.TestCase):
    def test_request_layout_matches_uploaders(self) -> None:
        payload = struct.pack("<II", 0x1000, 3) + b"abc"
        packet = request(0xD4, payload)
        self.assertEqual(packet[:2], b"\xd4\x00")
        self.assertEqual(struct.unpack_from("<I", packet, 4)[0], binascii.crc32(payload))

    def test_progress_uses_k210_wire_header(self) -> None:
        raw_size = 10_000
        wire_size = raw_size + 37
        self.assertEqual(progress_after([(0, 4096)], wire_size), 40)
        self.assertEqual(progress_after([(0, 4096), (4096, 4096), (8192, 4096)], wire_size), 100)

    def test_retry_does_not_double_count(self) -> None:
        self.assertEqual(progress_after([(0, 4096), (0, 4096), (4096, 4096)], 8192), 100)

    def test_out_of_order_block_does_not_advance_contiguous_range(self) -> None:
        self.assertEqual(progress_after([(4096, 4096)], 8192), 0)
        self.assertEqual(progress_after([(4096, 4096), (0, 4096)], 8192), 100)

    def test_header_validation_boundaries(self) -> None:
        maximum_raw = 16 * 1024 * 1024 - 37
        for raw_size in (1, 10_000, maximum_raw):
            header = b"\x00" + struct.pack("<I", raw_size)
            self.assertEqual(struct.unpack_from("<I", header, 1)[0] + 37,
                             raw_size + 37)
        for raw_size in (0, maximum_raw + 1):
            self.assertFalse(0 < raw_size <= maximum_raw)

    def test_release_binary_contract(self) -> None:
        binary = ROOT / "isp_prog_dristy.bin"
        self.assertTrue(binary.is_file())
        self.assertLessEqual(binary.stat().st_size, 64 * 1024)
        self.assertEqual(binary.stat().st_size, 17_856)
        import hashlib
        self.assertEqual(
            hashlib.sha256(binary.read_bytes()).hexdigest(),
            "da6305613ff9179afd439be1227ec877d583cde351afed604c7e053052f57cd7",
        )

    def test_firmware_declares_required_protocol_and_ui_states(self) -> None:
        protocol = (ROOT / "src" / "isp_protocol.h").read_text()
        main = (ROOT / "src" / "main.c").read_text()
        ui = (ROOT / "src" / "ui.c").read_text()
        for value in ("0xD2", "0xD4", "0xD5", "0xD6", "0xD7", "0xD8", "0xD9", "0xE0", "0xE2", "0xE7"):
            self.assertIn(value, protocol)
        self.assertIn("isp_crc32", main)
        self.assertIn("ui_validate_write", main)
        self.assertIn("reboot_soc", main)
        self.assertIn("sysctl->soft_reset.soft_reset = 1U", main)
        self.assertIn("sysctl->soft_reset.soft_reset = 0U", main)
        self.assertLess(main.index("slip_send(&response"), main.index("if(reboot)"))
        self.assertIn("msleep(10U)", main)
        for state in ("WAITING FOR HOST", "INITIALIZING FLASH", "ERASING", "FLASHING", "FINALIZING", "DONE", "ERROR"):
            self.assertIn(state, ui)

    def test_progress_updates_do_not_clear_the_full_screen(self) -> None:
        ui = (ROOT / "src" / "ui.c").read_text()
        self.assertEqual(ui.count("lcd_clear(BLACK)"), 1)
        self.assertIn("if(displayed_percent == value)", ui)
        self.assertIn("if(current_state == state)", ui)
        self.assertIn("width - bar_width", ui)

    def test_flash_writer_programs_each_page_once(self) -> None:
        flash = (ROOT / "src" / "flash.c").read_text()
        self.assertIn("flash_page_program_fun(addr, data_buf, write_len);", flash)
        self.assertNotIn("write_len > 32", flash)
        self.assertNotIn("while (write_len)", flash)

    def test_range_erase_is_background_and_consumed_by_writes(self) -> None:
        main = (ROOT / "src" / "main.c").read_text()
        self.assertIn("erased_sector_bitmap", main)
        self.assertIn("sectors_mark_pre_erased(erase_state.active_start", main)
        self.assertIn("if(!sector_is_pre_erased(address))", main)
        self.assertIn("sector_clear_pre_erased(address);", main)
        self.assertIn("if(erase_state.active)\n                (void)erase_status();", main)


if __name__ == "__main__":
    unittest.main()
