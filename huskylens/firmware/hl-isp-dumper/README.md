# HuskyLens ISP Stub

[![CI](https://github.com/aztechell/huskylens-isp-stub/actions/workflows/ci.yml/badge.svg)](https://github.com/aztechell/huskylens-isp-stub/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/aztechell/huskylens-isp-stub)](https://github.com/aztechell/huskylens-isp-stub/releases/latest)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)

Open-source second-stage K210 ISP for HuskyLens. It runs from SRAM at
`0x80000000`, remains compatible with the kflash protocol, writes the onboard
SPI flash, and displays the complete update process on the built-in 320x240
ST7789 screen.

The UI shows `WAITING FOR HOST`, flash initialization, erase activity,
flashing percentage and progress bar, finalization, errors, and `DONE` before
the device reboots.

## Download

Download `isp_prog_huskylens.bin` from the
[latest release](https://github.com/aztechell/huskylens-isp-stub/releases/latest),
or use the reproducible checked-in binary at the repository root.

## Build

The minimal Apache-2.0 K210 BSP needed by the stub is vendored under
`third_party/minsdk`. Bootstrap downloads Kendryte SDK commit
`02576ba67e8797444f3ee3f34c625b5ed048e707` and toolchain
`8.2.0-20190409` for Windows or Linux:

```powershell
py tools\bootstrap_deps.py
py tools\build.py
py -m unittest discover -s tests -v
```

On Linux:

```bash
python3 tools/bootstrap_deps.py
python3 tools/build.py
python3 -m unittest discover -s tests -v
```

Outputs are written to `build/isp_stub.elf`, `build/isp_stub.map`, and
`build/isp_prog_huskylens.bin`. The checked-in `isp_prog_huskylens.bin` is
refreshed by the same build. The build fails above the 64 KiB binary budget.

The current release artifact is 17,856 bytes with SHA-256
`da6305613ff9179afd439be1227ec877d583cde351afed604c7e053052f57cd7`.

Pushing a `v*` tag runs the release workflow, rebuilds and tests the stub, and
publishes the binary, checksum, ELF, and linker map as GitHub release assets.

## Protocol

Frames use the kflash SLIP envelope and CRC32 request layout. Supported
commands are `D2` NOP, `D4` flash write, `D5` reboot, `D6` baud switch,
`D7` flash initialization, `D8` range erase, and `D9` erase status. Responses
include `E0` OK, `E2` bad CRC, and `E7` busy. Existing uploaders require no new
commands.

The first address-zero block contains the K210 image header. Its little-endian
raw length at bytes 1-4 gives `wire_size = raw_size + 37`; committed contiguous
blocks drive the displayed percentage. Retries do not count twice. Later
KFPKG segments display `FINALIZING` until reboot.

## HuskyLens wiring

| Function | IO / peripheral |
|---|---|
| ISP UART RX/TX | IO4 / IO5, UART3 |
| LCD DC | IO18 / GPIOHS15 |
| LCD CS | IO19 / SPI0 SS3 |
| LCD SCLK | IO20 / SPI0 SCLK |
| LCD MOSI | IO21 / SPI0 D0 |
| LCD reset | IO22 / GPIOHS14 |
| LCD backlight | IO24 / GPIOHS13 |
| Boot flash | internal SPI3 |

See [NOTICE.md](NOTICE.md) for upstream provenance. Source is Apache-2.0.
