# Gate 0 — Stock firmware characterization

Measured on the physical unit, not quoted from documentation. Every number
below was produced by `tools/hl_probe.py` and `tools/hl_bench.py` against the
device described here.

## Unit under test

| Property | Value |
|---|---|
| Board | DFRobot SEN0305, standard (not PRO) |
| USB bridge | Silicon Labs CP2102N, VID:PID `10c4:ea60` |
| Bridge serial | `84d8a094775dec11961b781d25bfaa52` |
| Enumerated as | `/dev/ttyUSB0`, USB path `1-2` (root hub port, direct) |
| Host | Ubuntu 22.04 in distrobox `auv-ros2`, ROS 2 Humble |

`REQUEST_IS_PRO` (0x3B) returns `00 00`, confirming a standard SEN0305 rather
than a PRO SEN0336.

## Findings

### 1. The USB serial link runs at 3,000,000 baud, and only there

A KNOCK was attempted at 9600, 57600, 115200, 230400, 1000000, 2000000 and
3000000 baud. **Only 3,000,000 produced a response**; every other rate was
completely silent.

This matters because the device's own General Settings menu offers just 9600,
115200 and 1000000 as Serial protocol rates. The rate that actually works over
USB is not in that menu at all. It matches the default in DFRobot's own
`huskylib.py`, which hardcodes `speed=3000000` for serial mode.

**Working hypothesis:** the USB path (K210 UART3 on IO4/IO5, behind the
CP2102N) is fixed at 3 Mbaud in firmware, and the menu's Protocol Type setting
governs only the 4-pin Gravity connector (UART1 on IO34/IO35). Not yet
confirmed — confirming it requires changing the menu setting and re-running the
baud scan, which is worth doing before we rely on it.

### 2. The first command after opening the port is always discarded

Across four consecutive fresh sessions, the first command sent after `open()`
received no reply, and the second always succeeded:

```
session 0: first=DROPPED  second=OK
session 1: first=DROPPED  second=OK
session 2: first=DROPPED  second=OK
session 3: first=DROPPED  second=OK
```

This is undocumented. It is almost certainly responsible for a large share of
the "SEN0305 doesn't respond" reports on DFRobot's forums, since a naive
client that sends one KNOCK and gives up will conclude the device is dead.

`SerialTransport` now absorbs this with a throwaway KNOCK on open, so callers
never observe it.

### 3. Once warm, the link is extremely reliable

KNOCK was issued 60 times across inter-command gaps of 0, 20, 50, 100 and
200 ms, at read timeouts of 300 and 600 ms. **60 of 60 returned `RETURN_OK`**,
with zero `RETURN_BUSY` and zero timeouts. There is no minimum inter-command
delay for KNOCK.

The `RETURN_BUSY` seen during early bring-up was a consequence of the
first-command drop desynchronising the request/response pairing, not of polling
too fast.

### 4. The serial link is not the bottleneck — by a factor of 36

| Request | Poll rate | Device frame rate | p50 latency | p95 latency | Errors |
|---|---|---|---|---|---|
| `REQUEST` (0x20) | 1135.9 Hz | 26.5 fps | 0.86 ms | 1.01 ms | 0 |
| `REQUEST_BLOCKS` (0x21) | 1083.3 Hz | 30.0 fps | 0.94 ms | 1.02 ms | 0 |
| `REQUEST_ARROWS` (0x22) | 1087.0 Hz | 30.0 fps | 0.94 ms | 1.02 ms | 0 |
| `REQUEST_LEARNED` (0x23) | 1125.3 Hz | 30.0 fps | 0.84 ms | 1.02 ms | 0 |

Six seconds per test, scene held constant, nothing learned in the active
algorithm.

"Device frame rate" is derived from the `frame_number` field of `RETURN_INFO`,
so it measures the camera pipeline independently of how fast we poll.

Two conclusions:

- **The pipeline genuinely runs at 30.0 fps** in the as-found algorithm. DFRobot's
  marketing claim is accurate here, and the widely repeated community figure of
  10-12 fps does not apply to this algorithm on this firmware.
- **The link sustains ~1,100 round trips per second at sub-millisecond latency**,
  roughly 36 times the rate at which the device produces new frames. Polling
  faster buys nothing; the limit is entirely inside the firmware.

This is the central justification for the firmware work: no amount of protocol
or baud-rate tuning can extract more from this device. The ceiling is the stock
firmware's, and the only way past it is to replace it.

### 5. `REQUEST_FIRMWARE_VERSION` (0x3C) is an acknowledged stub

The protocol document lists 0x3C under a bare heading with no frame layout, no
response command and no payload description. On this firmware it returns
`RETURN_OK` (0x2E) with an **empty data field**. The command is implemented far
enough to acknowledge, but it carries no version information, so there is no
supported way to query the firmware version over the wire.

## Reproducing

```bash
cd dristy
PYTHONPATH=src python3 -m unittest discover -s tests   # codec vs. published examples
python3 tools/hl_probe.py --json artifacts/probe.json  # baud scan + identity
python3 tools/hl_bench.py --baud 3000000 --duration 6 --json artifacts/benchmark.json
```

`tools/hl_sweep.py` covers the undocumented command space and has not yet been
run; it refuses every state-changing command by construction.

## Recovery assets mirrored

`vendor/SEN0305Uploader` holds 11 `.kfpkg` packages (V0.3.3 through
V0.5.3Alpha1, including the `Class` object-classification variants) and 5 bare
`.bin` images, with SHA-256 sums recorded in `artifacts/firmware_checksums.txt`.
`vendor/dristy-isp-stub/isp_prog_dristy.bin` provides the display-aware
second-stage ISP.
