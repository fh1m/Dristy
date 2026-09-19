# Gate 2 — Custom firmware built on Linux and running on the device

The device now runs firmware we built ourselves, from source, on Linux.

## Linux port

Upstream Dristy builds on Windows only. Three portability changes were
needed, none of which alter behaviour:

1. **Toolchain selection.** `bootstrap_deps.py` now picks the Kendryte release
   archive by host platform. The Linux build is
   `kendryte-toolchain-ubuntu-amd64-8.2.0-20190409.tar.xz`, SHA-256
   `6b7b9f65...`. It runs natively on Ubuntu 22.04; the `libisl.so.19` problem
   often reported for this toolchain did not occur, because the bundle ships
   its own libraries.

2. **Host compiler.** The pinned mingw-w64 GCC exists to build MicroPython's
   qstr and frozen-module generators, which run on the *build* machine, not the
   K210. On POSIX the system compiler already does that job, so it is
   discovered rather than downloaded. Distribution GCC versions are
   deliberately not pinned — doing so would reject every supported
   distribution. Ubuntu 22.04's GCC 11.4.0 works.

3. **`tomllib` fallback.** `check_arch.py`, `gen_capability_inventory.py` and
   `board_contract.py` fall back to `tomli` on Python 3.10 and older.

`env.ps1` gains a POSIX sibling, `env.sh`.

### The one non-obvious bug

`build_firmware.py` applies a MicroPython patch with `git apply --directory`.
Git resolves that option against the **repository root**, and separately
refuses paths outside the invocation prefix. Upstream assumes the Dristy
tree *is* the repository root, which holds for a standalone clone. When
Dristy sits inside a larger repository, the computed path is wrong and git
prints:

```
Skipped patch 'build/micropython_embed/py/runtime.c'.
```

and **exits 0**. The build then fails much later with the unrelated-looking
message `MicroPython native iterator poll patch is incomplete`. Anchoring
`--directory` to the output of `git rev-parse --show-toplevel` fixes it for
both layouts.

This is worth upstreaming: the failure is silent, and the error surfaces far
from its cause.

## Build output

```
dist/dristy-full-sen0305.bin
  size    1,543,544 bytes
  sha256  2a854c6a188ed25c082d3c2aaa8acf7e30d44b4e3644145bbf0d5368f88da13f
  release_qualified: true
```

Twelve apps: terminal, camera, qr-camera, face-detect, apriltag,
object-detect, files, buttons, pong, settings, sleep, **micropython**.

Before modifying anything, the upstream ISP stub was rebuilt on Linux and
hashed `da6305613ff9179abd439be1227ec877d583cde351afed604c7e053052f57cd7` —
byte-identical to the binary upstream checked in from Windows. That validated
the toolchain independently of any source change.

## Flashed and verified

Flashing entered the BootROM on the `kd233` polarity, after `dan` failed, which
matches the Gate 1 finding. The image wrote at 2,000,000 baud in about 25
seconds and the device rebooted into it.

The firmware exposes a text command interface over the USB UART. `HKHELP`
lists:

```
HKHELP HKSHOT HKFRAME HKCAMINFO HKFPS/HKFPSON/HKFPSOFF HKCAMPROBE HKCAMREGS
HKCAMDVP HKCAMBAR HKCAMERA HKQRINFO HKQR/HKQRCAM HKQRDECODE HKFACEINFO
HKTAG/HKTAGINFO HKOBJECT/HKOBJECTINFO HKSETTINGS HKMPRUN HKMPTEST HKMPSTOP
HKMPSTATUS HKMPLOG HKMPLIST HKMPFORMAT-CONFIRM HKLINKINFO HKLINKUART
HKLINKI2C HKLINK9600 HKLINK115200 HKLINK1000000 HKMENU HKPING
```

`HKFRAME` streams the raw camera RGB565 frame and `HKSHOT` streams a BMP
screenshot over USB — both of which the stock firmware never offered.

## Hardware facts confirmed on this unit

**Camera is an OV2640.** The long-standing OV2640-vs-GC0328 question is settled
for this board:

```
[CAM] sccb addr=0x60 mid=0x7FA2 pid=0x2642
[CAM] sensor OV2640
```

**Camera pin map:**

| Signal | Pin |
|---|---|
| PCLK | IO47 |
| XCLK | IO46 (33,583,333 Hz) |
| HREF | IO45 |
| PWDN | IO44 |
| VSYNC | IO43 |
| RST | IO42 |
| SCCB | IO40 / IO41 |

**Other pins reported at boot:** LCD `IO18`=DC, `IO22`=RST, `IO19`=SPI0_SS3;
buttons LEFT/OK/RIGHT/BACK = 1/2/4/8; illumination LED candidate `IO23`
(PWM2_CH3); RGB LED candidates `IO32`/`IO30`/`IO31` (PWM2_CH0/1/2); SD
candidates `IO27`=SCLK, `IO28`=D0, `IO26`=D1, `IO29`=CS. No SD card is present
(`CMD0 failed r1=0xFF`).

**The Gravity connector is a different UART from USB.** Boot prints:

```
[LINK] UART1 115200 IO34/IO35
```

This directly supports the Gate 0 hypothesis: the 4-pin Gravity connector is
UART1 on IO34/IO35, while USB reaches the K210 over UART3 on IO4/IO5. That is
why the device's Protocol Type menu — which offers 9600/115200/1000000 —
never matched the 3,000,000 baud the USB link actually required. The two paths
are independent.

**Settings live at `0x7FE000`/`0x7FF000`**, a two-slot journal, consistent with
the isolated single sectors seen near the top of the Gate 1 flash map.

## Camera performance

In the camera app the sensor sustains roughly 19-25 fps at 320x240 RGB565,
with compose about 6.6 ms and present about 31 ms per frame. Present dominates,
so LCD transfer is the limiting stage in this path rather than capture.

## Restore

The device can be returned to stock at any time from
`artifacts/stock_flash_dump1.bin` (byte-exact, `e98ace89...`) or from any of
the 11 mirrored official packages.
