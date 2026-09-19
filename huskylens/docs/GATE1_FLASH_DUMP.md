# Gate 1 — Flash readback and recovery

Flashing a HUSKYLENS has always been a one-way door. The stock kflash
second-stage ISP is write-only: it implements `D4` write, `D7` init, `D8` erase
and `D9` erase status, but has **no read command**. HackyLens' own hardware
evidence records this as a limitation ("the selected ISP stub exposes no flash
read command, so `--verify` reported that readback was unavailable"), and no
public HUSKYLENS flash-dumping tool could be found.

That means you could restore DFRobot's *published* packages, but never the
exact bytes that were on your own device — in particular the model and service
region at `0x800000`, which DFRobot has never published in any form.

This gate closes that hole.

## What was built

`firmware/hl-isp-dumper` extends the Apache-2.0
[huskylens-isp-stub](https://github.com/aztechell/huskylens-isp-stub) with two
commands, chosen from opcodes the kflash protocol leaves unclaimed so existing
uploaders are unaffected:

| Opcode | Name | Behaviour |
|---|---|---|
| `0xD3` | `ISP_FLASH_READ` | Reads up to 4096 bytes from a flash address |
| `0xDA` | `ISP_FLASH_JEDEC` | Returns the 3-byte JEDEC ID |

Responses to these two carry a payload after the normal two-byte header: a
CRC-32 over the data, then the data. Every other response stays exactly two
bytes, so the change is backward compatible.

The underlying `flash_read_data()` and `flash_read_jedec_id()` already existed
in the vendored flash driver — they were simply never exposed over the wire.

`tools/hl_dump.py` drives it from the host.

## Two things that were not obvious

**The stub lives in SRAM, so the port must never be reopened.** The first
implementation shelled out to `kflash -s` to boot the stub, then opened the
port itself to talk to it. That never worked: closing and reopening the port
pulses DTR, which is wired to the K210 RESET pin, which wipes the SRAM-resident
stub. The symptom is a stub that boots successfully and then answers nothing at
all. `hl_dump.py` therefore implements the BootROM handshake itself — reset,
greet, `0xC3` memory write, `0xC5` boot — on the same open handle it then uses
for the dump.

**The baud-switch acknowledgement arrives at the new rate.** `0xD6` calls
`uart_configure()` and *then* returns `ISP_OK`, so the acknowledgement is
transmitted at the new baud while the host is still listening at the old one.
It looks like a timeout. `switch_baud()` fires the command without waiting,
re-rates the host, and confirms with a NOP.

**Only the `kd233` reset polarity enters the BootROM on this board.** Of
kflash's profiles, `kd233`, `goE` and `maixduino` work; `dan`, `goD`, `bit` and
`trainer` all fail to get a greeting. Tools defaulting to `dan` will appear to
find no K210 at all.

## Result

```
JEDEC ID ef 60 18 -> manufacturer 0xef, capacity 16 MiB
Wrote 16777216 bytes in 99.6s   (164 KiB/s at 2,000,000 baud)
```

Two full dumps were taken in separate sessions and are **byte-identical**:

```
e98ace89567a9f18529d99c051d7104c8761d475753211c1855adde8d4f0c42d  stock_flash_dump1.bin
e98ace89567a9f18529d99c051d7104c8761d475753211c1855adde8d4f0c42d  stock_flash_dump2.bin
```

The JEDEC ID settles the open question about board revisions for this unit:
`0x18` is log2(capacity), so this is a 16 MiB part, and `0xEF` is Winbond. The
8 MiB revision that HackyLens defends against did not appear here.

## Observed flash map

Derived from the dump by classifying each 4 KiB sector as data, erased
(`0xFF`) or zeroed. 12,582,912 bytes — exactly 12 MiB, 75% — are occupied.

| Range | Size | Contents |
|---|---|---|
| `0x000000`-`0x0CA000` | 827,392 | Boot image. Header flag `0x00` (unencrypted), declared length 823,814 bytes |
| `0x0CA000`-`0x1BB000` | 987,136 | erased |
| `0x1BB000`-`0x5DA000` | 4,321,280 | data |
| `0x5DB000`-`0x5F7000` | 114,688 | data |
| `0x5F8000`-`0x5FE000` | 24,576 | data |
| `0x5FF000`-`0xC5C000` | 6,672,384 | data (largest single region; spans the `0x800000` model/service area) |
| `0xC60000`-`0xC71000` | ~57 KiB | several small data sectors separated by erased ones |
| `0xE80000`-`0xED7000` | 356,352 | data |
| `0xF80000`-`0xFB1000` | 200,704 | data |
| `0xFE0000`, `0xFF0000` | 4,096 each | isolated sectors, consistent with settings journal slots |

The header at offset 0 confirms the K210 boot image format that the stub's
progress UI relies on: byte 0 is the AES flag and bytes 1-4 are the
little-endian raw length, here 823,814, which matches the 827,392-byte occupied
region once the 37-byte wire overhead and sector padding are accounted for.

## Recovery position

We can now restore this device three ways, in decreasing fidelity:

1. **Byte-exact**, from `stock_flash_dump1.bin` — including DFRobot's
   unpublished model region. This did not previously exist for any HUSKYLENS.
2. From the mirrored official `.kfpkg` packages in `vendor/HUSKYLENSUploader`
   (11 packages, checksums in `artifacts/firmware_checksums.txt`).
3. Via the HuskyLens Uploader's "Reset Memory" function on Windows.

Additionally, the K210 BootROM is mask ROM and cannot be erased, so as long as
IO16 and IO4/IO5 reach the CP2102N, a bad flash is always recoverable over USB.
There is no fuse lockout on this path.

Writing custom firmware is now a reversible operation.

## Reproducing

```bash
cd huskylens/firmware/hl-isp-dumper && python3 tools/bootstrap_deps.py && python3 tools/build.py
cd ../.. && python3 tools/hl_dump.py --output artifacts/stock_flash_dump1.bin
```

The upstream stub builds byte-identically on Linux
(`da6305613ff9179abd439be1227ec877d583cde351afed604c7e053052f57cd7`, matching
the checked-in binary built on Windows), which validated the toolchain before
any modification was made.
