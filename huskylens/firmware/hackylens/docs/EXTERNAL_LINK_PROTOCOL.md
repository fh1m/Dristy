---
contract-id: hackylens.external-link
owner: device-protocols
version: 1.0.0
stability: experimental
wire-major: 1
---

# HackyLens External Link Protocol v1

This is a HackyLens-native protocol. It deliberately does not emulate the
original HUSKYLENS command set or packet layout.

## Physical port

The four-pin `T R - +` connector is independent from the UART3 debug/PC
connection. Reverse engineering maps the signal pins as follows:

| Connector | Descriptor route roles | UART mode | I2C mode |
|---|---|---|---|
| `T` | `IO_EXTERNAL_UART_T` / `IO_EXTERNAL_I2C_T` | UART1 TX, 9600/115200/1000000 8N1 | I2C0 SDA |
| `R` | `IO_EXTERNAL_UART_R` / `IO_EXTERNAL_I2C_R` | UART1 RX, 9600/115200/1000000 8N1 | I2C0 SCL |
| `-` | — | Ground | Ground |
| `+` | — | 3.3–5.0 V power input | 3.3–5.0 V power input |

The selected board descriptor is the sole source of physical pin numbers.
The signal mapping has medium confidence from the recovered board setup. Treat
the signal lines as 3.3 V logic. DFRobot specifies a 3.3–5.0 V supply range and
typical consumption of 320 mA at 3.3 V or 230 mA at 5 V. Its automatic source
selector permits USB and four-pin power simultaneously and gives USB priority;
therefore `+` is a supply input and is not expected to source voltage under USB
power.
I2C mode uses the camera as a 7-bit slave at address `0x32`. UART is the default.
Select the mode through Settings / External Link or through the PC debug UART
with `HKLINKUART` and `HKLINKI2C`. Settings / UART Speed selects 9600, 115200,
or 1000000 baud and persists the choice. `HKLINK9600`, `HKLINK115200`, and
`HKLINK1000000` select that speed, switch to UART, and save both settings.

## Frame

All multibyte integers are little-endian. UART and I2C carry exactly the same
frame:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | magic bytes `H`, `K` |
| 2 | 1 | protocol version, currently `1` |
| 3 | 1 | message type |
| 4 | 2 | caller-selected sequence number |
| 6 | 2 | payload length, maximum 160 bytes |
| 8 | N | payload |
| 8+N | 2 | CRC-16/CCITT-FALSE over bytes 2 through 7+N |

CRC parameters are polynomial `0x1021`, initial value `0xFFFF`, no reflection,
and no final XOR.

UART is a byte stream; the device resynchronizes on `HK`. For I2C, the master
writes one complete request transaction and waits at least 20 ms after its
write STOP. Read the 8-byte header, then the declared payload plus
the 2-byte CRC. The response cursor continues across multiple I2C read
transactions, allowing masters with 32-byte Wire buffers to read in chunks.
A new valid request prepares a new response and resets the cursor to byte zero;
bytes clocked after the complete response are zero.

## Commands

| Request | Response | Payload |
|---|---|---|
| `0x01 GET_INFO` | `0x81 INFO` | none |
| `0x02 GET_RESULTS` | `0x82 RESULTS` | none |
| `0x03 PING` | `0x83 PONG` | arbitrary bytes, echoed |
| unsupported/malformed | `0xFF ERROR` | one-byte error code |

`INFO` contains `capabilities:u16`, `max_items:u8`, `active_transport:u8`
(`0=UART`, `1=I2C`), `i2c_address:u8`, one reserved byte, and `uart_baud:u32`.
Capability bits 0–3 mean UART, I2C, BLOCK, and ARROW.

## Results and unified items

`RESULTS` starts with `frame_id:u32`, `source:u8`, `count:u8`, `width:u16`, and
`height:u16`. Source `0` is no active result, `1` is face detection, `2` is QR,
`3` is AprilTag, `4` is VOC20 object detection, and `255` is reserved for user
algorithms.

Each result is a fixed 16-byte item:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | kind: `1=BLOCK`, `2=ARROW` |
| 1 | 1 | flags, currently zero |
| 2 | 2 | algorithm-local ID |
| 4 | 2 | x0 |
| 6 | 2 | y0 |
| 8 | 2 | x1 |
| 10 | 2 | y1 |
| 12 | 2 | confidence, 0–1000 |
| 14 | 2 | reserved, zero |

For a BLOCK, `(x0,y0)` and `(x1,y1)` are the top-left and bottom-right corners.
For an ARROW they are the start and end points. Coordinates use the `width` by
`height` source canvas returned in the same message. FACE DETECT currently
publishes up to eight BLOCK items and clears them on app exit. APRILTAG uses
the native tag ID. OBJECT DETECT uses the VOC20 class ID `0..19` and reports
confidence on the existing `0..1000` scale; the item layout is unchanged.
