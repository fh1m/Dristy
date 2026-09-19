# Dristy SEN0305 Hardware & Stock Firmware — Deep Report

## Key discoveries

### Four undocumented protocol commands

The Arduino library header exposes commands DFRobot never documented:

| Hex | Name | Direction | Notes |
|---|---|---|---|
| `0x31` | SEND_PHOTO | →HL | Photo transfer over the link, not to SD |
| `0x38` | SEND_SCREENSHOT | →HL | Screenshot over the link |
| `0x3A` | LOAD_AI_FRAME_FROM_USB | →HL | **Push a frame INTO the device for inference** — the Mind+ online-mode mechanism |
| `0x3D` | REQUEST_SENSOR | →HL | Three int16s pushed to device, likely for overlay. Collides with RETURN_BUSY (disambiguate by direction) |

`0x3A` is the most interesting — it means the stock firmware already has a code
path for host-fed inference frames.

### No public schematic exists

DFRobot publishes only a layout/silkscreen PDF with reference designators and
no values or netlist. ~49 caps, 4 diodes, 7 inductors, 11 transistors/regulators.

### Power is the #1 field failure

320 mA @3.3V / 230 mA @5.0V typical. Brownouts on Learn in Object Recognition
are consistently resolved by switching to dedicated 5V USB supply. K210 core
rail specced at 2A max but regulator sized for typical KPU load only.

### SEN0305 vs SEN0336 PRO — only the camera differs

Same K210, same RAM, same screen, same power. PRO has OV5640 5MP instead of
OV2640 2MP. QR/barcode being PRO-exclusive is a firmware gate, not hardware.
Custom firmware on SEN0305 implementing QR decode (as Dristy does via quirc)
is entirely feasible.

### Object classification shipped as separate firmware

DFRobot could not fit classification and detection models simultaneously in
8 MiB. Merged into single image from V0.5.1a onward but the memory pressure
remains — plan for model swapping from flash, not co-residency.

## Complete protocol command table

| Hex | Name | Dir | Data |
|---|---|---|---|
| `0x20` | REQUEST | →HL | — (all blocks + arrows) |
| `0x21` | REQUEST_BLOCKS | →HL | — |
| `0x22` | REQUEST_ARROWS | →HL | — |
| `0x23` | REQUEST_LEARNED | →HL | — (ID ≥ 1 only) |
| `0x24` | REQUEST_BLOCKS_LEARNED | →HL | — |
| `0x25` | REQUEST_ARROWS_LEARNED | →HL | — |
| `0x26` | REQUEST_BY_ID | →HL | u16 id |
| `0x27` | REQUEST_BLOCKS_BY_ID | →HL | u16 id |
| `0x28` | REQUEST_ARROWS_BY_ID | →HL | u16 id |
| `0x29` | RETURN_INFO | ←HL | 10B: nResults, nLearnedIDs, frameNum, reserved |
| `0x2A` | RETURN_BLOCK | ←HL | 10B: xCenter, yCenter, width, height, ID |
| `0x2B` | RETURN_ARROW | ←HL | 10B: xOrigin, yOrigin, xTarget, yTarget, ID |
| `0x2C` | REQUEST_KNOCK | →HL | — |
| `0x2D` | REQUEST_ALGORITHM | →HL | u16 alg (0-6, 7-8=PRO) |
| `0x2E` | RETURN_OK | ←HL | — |
| `0x2F` | REQUEST_CUSTOMNAMES | →HL | u8 id, u8 len+1, chars, 0x00 |
| `0x30` | REQUEST_PHOTO | →HL | — (photo → SD) |
| `0x31` | REQUEST_SEND_PHOTO | →HL | ⚠️ undocumented |
| `0x32` | REQUEST_SEND_KNOWLEDGES | →HL | u16 fileNum |
| `0x33` | REQUEST_RECEIVE_KNOWLEDGES | →HL | u16 fileNum |
| `0x34` | REQUEST_CUSTOM_TEXT | →HL | u8 len, u8 xFlag, u8 x, u8 y, chars |
| `0x35` | REQUEST_CLEAR_TEXT | →HL | — |
| `0x36` | REQUEST_LEARN | →HL | u16 id |
| `0x37` | REQUEST_FORGET | →HL | — |
| `0x38` | REQUEST_SEND_SCREENSHOT | →HL | ⚠️ undocumented |
| `0x39` | REQUEST_SAVE_SCREENSHOT | →HL | — |
| `0x3A` | REQUEST_LOAD_AI_FRAME_FROM_USB | →HL | ⚠️ undocumented |
| `0x3B` | REQUEST_IS_PRO / RETURN_IS_PRO | ↔ | u16 (1=Pro, 0=Standard) |
| `0x3C` | REQUEST_FIRMWARE_VERSION | →HL | u8 len+1, ascii (host sends its version) |
| `0x3D` | REQUEST_SENSOR | →HL | ⚠️ u16 a, u16 b, u16 c |
| `0x3D` | RETURN_BUSY | ←HL | — (direction disambiguates) |
| `0x3E` | RETURN_NEED_PRO | ←HL | — |

Framing: `0x55 0xAA | addr(0x11) | dataLen | cmd | data | checksum(low byte of sum)`

I2C: address 0x32, same framing. Device is a dumb byte-stream parser hunting
for 0x55 0xAA — no register file. SMBus write prepends 0x0C which is discarded
as pre-sync garbage.

## Stock firmware algorithms

| ID | Algorithm | Returns | Notes |
|---|---|---|---|
| 0 | Face Recognition | Blocks | KPU face detect + learned embedding |
| 1 | Object Tracking | Blocks | Template with continuous refinement |
| 2 | Object Recognition | Blocks | Fixed 20-class VOC, not user-extensible |
| 3 | Line Tracking | Arrows | Single-colour, path prediction |
| 4 | Colour Recognition | Blocks | IDs in learn order |
| 5 | Tag Recognition | Blocks | AprilTag TAG36H11 only |
| 6 | Object Classification | Blocks | No geometry, always returns some class |
| 7 | QR Code | — | PRO only |
| 8 | Barcode | — | PRO only |

Learned-object limit: ~40 per function. 5 saved models per algorithm on SD.

## OV2640 vs GC0328

DFRobot spec table says "OV2640 / GC0328" — batch dependent. Our unit is
confirmed OV2640 (SCCB 0x60, MID 0x7FA2, PID 0x2642). GC0328 uses SCCB 0x42
with chip ID 0x9d. Runtime probe of both addresses is cheap. Dristy
hardcodes OV2640 only — GC0328 support would be new work.

## Flash layout (stock)

| Offset | Size | Contents |
|---|---|---|
| 0x000000 | ~8 MiB | Firmware image |
| 0x7FE000 | 4 KiB | Settings journal slot 0 |
| 0x7FF000 | 4 KiB | Settings journal slot 1 |
| 0x800000 | 4.44 MiB | Stock KPU models |
| 0xC70000 | 3.56 MiB | Free on 16 MiB parts |

8 MiB flash variants exist — `flash_layout.json` gates the user filesystem on
JEDEC discovery of exactly 16 MiB.

## No existing ROS/ROS2 package

No `dristy_ros`, no driver node, nothing published. A Dristy ROS2 driver
would be genuinely novel.
