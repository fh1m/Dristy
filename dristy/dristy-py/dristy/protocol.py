"""
DLP (Dristy Link Protocol) wire format encoder/decoder.

Wire format (same framing as stock Dristy):
    [0x55] [0xAA] [0x11 addr] [data_len] [cmd] [data...] [checksum]

Checksum = sum of bytes from addr to last data byte, mod 256.
"""

import struct
from typing import Optional, Tuple

# DLP command IDs
CMD_SET_MODE        = 0x40
CMD_GET_MODE        = 0x41
CMD_GET_TRACKS      = 0x42
CMD_GET_FLOW        = 0x43
CMD_GET_TAGS        = 0x44
CMD_GET_QR          = 0x45
CMD_LOAD_MODEL      = 0x46
CMD_SET_THRESHOLD   = 0x47
CMD_SET_CLOCK       = 0x48
CMD_GET_PERF        = 0x49
CMD_PUSH_FRAME      = 0x4A
CMD_GET_RAW_FRAME   = 0x4B
CMD_SET_ROI         = 0x4C
CMD_SET_COLOUR_THRESH = 0x4D
CMD_GET_BLOBS       = 0x4E
CMD_IDENTIFY        = 0x4F

CMD_CTRL_STREAM_ON  = 0x60
CMD_CTRL_STREAM_OFF = 0x61
CMD_CTRL_TARGET     = 0x62
CMD_CTRL_FULL       = 0x63
CMD_SET_TARGET_ID   = 0x64
CMD_SET_TARGET_CLS  = 0x65
CMD_SET_BAUD        = 0x66
CMD_GET_RESULT      = 0x67

CMD_BENCH_ENABLE    = 0x68
CMD_BENCH_REPORT    = 0x69
CMD_BENCH_RESET     = 0x6A
CMD_BENCH_PRINT     = 0x6B

# LCD and display commands
CMD_LCD_CONTROL     = 0x70
CMD_LCD_BRIGHTNESS  = 0x71

# Camera ISP commands
CMD_CAM_PRESET      = 0x72
CMD_CAM_SET_PARAM   = 0x73
CMD_CAM_GET_PARAMS  = 0x74

# LED commands
CMD_LED_MODE        = 0x76
CMD_LED_BRIGHTNESS  = 0x77
CMD_LED_COLOR       = 0x78
CMD_LED_ILLUMINATION = 0x79

# Config persistence
CMD_CONFIG_SAVE     = 0x7A
CMD_CONFIG_LOAD     = 0x7B
CMD_CONFIG_RESET    = 0x7C

# ArUco / fiducial config
CMD_ARUCO_SET_DICT  = 0x7D
CMD_ARUCO_SET_SIZE  = 0x7E

# Motion detection config
CMD_MOTION_SENSITIVITY = 0x7F

# Stock Dristy commands (0x20-0x3F)
CMD_KNOCK           = 0x2C
CMD_RETURN_OK       = 0x2E

# Return IDs
RET_TRACK           = 0x50
RET_FLOW            = 0x51
RET_TAG             = 0x52
RET_QR              = 0x53
RET_PERF            = 0x54
RET_BLOB            = 0x55
RET_MODE            = 0x56
RET_IDENTITY        = 0x57
RET_CAM_PARAMS      = 0x58
RET_LCD_STATE       = 0x59

# Control output sync
CTRL_SYNC           = 0xD5
CTRL_HEARTBEAT      = 0x01
CTRL_TARGET         = 0x02
CTRL_TRACKS         = 0x03
CTRL_TAGS           = 0x04
CTRL_FLOW           = 0x05
CTRL_FULL_FRAME     = 0x06

ADDR = 0x11
SYNC = b"\x55\xaa"


def build_packet(cmd: int, data: bytes = b"") -> bytes:
    """Build a DLP wire packet."""
    payload = bytes([ADDR, len(data), cmd]) + data
    checksum = sum(payload) & 0xFF
    return SYNC + payload + bytes([checksum])


def parse_packet(buf: bytes) -> Optional[Tuple[int, bytes, int]]:
    """Parse a DLP packet from buffer.

    Returns (cmd, data, consumed_bytes) or None if incomplete/invalid.
    """
    idx = buf.find(SYNC)
    if idx < 0 or idx + 5 > len(buf):
        return None

    addr = buf[idx + 2]
    data_len = buf[idx + 3]
    cmd = buf[idx + 4]

    total = idx + 5 + data_len + 1  # sync(2) + addr(1) + len(1) + cmd(1) + data + checksum(1)
    if total > len(buf):
        return None

    data = buf[idx + 5: idx + 5 + data_len]
    expected_sum = sum(buf[idx + 2: idx + 5 + data_len]) & 0xFF
    actual_sum = buf[idx + 5 + data_len]

    if expected_sum != actual_sum:
        return None

    return (cmd, data, total)


def crc8_maxim(data: bytes) -> int:
    """CRC-8/MAXIM for control output packets."""
    crc = 0x00
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ 0x31) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc


def parse_ctrl_packet(buf: bytes) -> Optional[Tuple[int, bytes, int]]:
    """Parse a control output packet.

    Wire: [0xD5 sync] [type] [len_lo] [len_hi] [payload] [crc8]
    Returns (type, payload, consumed_bytes) or None.
    """
    idx = 0
    while idx < len(buf):
        if buf[idx] == CTRL_SYNC:
            break
        idx += 1

    if idx + 4 >= len(buf):
        return None

    msg_type = buf[idx + 1]
    payload_len = buf[idx + 2] | (buf[idx + 3] << 8)
    total = idx + 4 + payload_len + 1  # sync + type + len(2) + payload + crc

    if total > len(buf):
        return None

    payload = buf[idx + 4: idx + 4 + payload_len]
    expected_crc = crc8_maxim(buf[idx + 1: idx + 4 + payload_len])
    actual_crc = buf[idx + 4 + payload_len]

    if expected_crc != actual_crc:
        return None

    return (msg_type, payload, total)
