# Phase 2.13 SEN0305 hardware acceptance

This procedure qualifies the five Phase 2 capabilities on HUSKYLENS/SEN0305.
Each observation is retained with the exact image that was tested. A closure
build may carry forward unaffected observations after an explicit impact review;
it does not require a complete physical rerun after every unrelated change.

Current status: **IN PROGRESS**. The live checklist is maintained in
[Phase 2 physical status](PHASE2_PHYSICAL_STATUS.md). This document is the
runbook, not the physical evidence itself.

## Non-negotiable gate

A passing closure needs:

- one immutable `phase2-candidate-result.json` copied from a green Phase 2
  rolling result;
- the exact closure image identity, with matching image, composition,
  capability-inventory, and attestation hashes;
- the candidate commit and its green `Release firmware` workflow URL;
- the pinned Kendryte compiler archive/version and SDK revision, plus the host
  Python and CMake versions used for the candidate build;
- one recorded SEN0305 board serial/revision and operator identity;
- a fixture with an ID and a checked-in schematic, providing both UART TX/RX
  loopback and a known 7-bit I2C target;
- a per-check ledger that retains the tested image identity and raw/timing logs;
- an impact review mapping the closure diff to repeated and carried checks;
- every required result marked `pass` in
  `docs/evidence/phase2-hardware-smoke.json`;
- a successful `python tools/check_phase2_hardware.py` invocation.

Host tests never replace the external electrical fixture. A missing UART
loopback or known I2C target leaves Phase 2.13 open.

The firmware version remains part of each tested identity. A version-only or
unrelated app/UI change requires the automated gate and closure boot sanity,
not an automatic UART/I2C/lights rerun. Provider, HAL, routing, board,
toolchain, composition, resource-assumption, or uncertain cross-cutting changes
repeat the affected physical checks.

## 1. Select the candidate

Start from a clean candidate commit. Run the complete automated Phase 2 gate,
including both SEN0305 profiles, capability-absent diagnostics, Cube compile
conformance, architecture evidence, shared fake/K210 suites, and resource
evidence. The push workflow must be green.

After that exact result is green and before changing any file in its source
scope, make the immutable candidate snapshot:

```powershell
Copy-Item -LiteralPath docs\evidence\phase2-result.json `
  -Destination docs\evidence\phase2-candidate-result.json
Get-FileHash -Algorithm SHA256 `
  -LiteralPath docs\evidence\phase2-candidate-result.json
git rev-parse HEAD
```

Record the full 40-character commit and the workflow run URL. From
`builds.profiles.full` in the candidate result, record these fields without
retyping or rounding them:

- `image.raw_bytes` and `image.sha256`;
- `composition_sha256`;
- `capabilities_sha256`;
- `attestation_sha256`.

Record the pinned compiler archive SHA-256, compiler version, and SDK revision
from `docs/evidence/phase2-baseline.json`. Record the actual host Python and
CMake versions used for the candidate build. The validator rejects a pinned
compiler or SDK identity that differs from the Phase 2 baseline.

The local `build/huskylens-sen0305/hackylens-full.bin` must have the same byte
count and SHA-256 before it is flashed. The firmware version recorded in the
hardware report must equal the repository `VERSION` file.

Do not edit `phase2-candidate-result.json` after selection. If automated
evidence, firmware source, composition, toolchain, or `VERSION` changes, select
a new closure snapshot and record an impact review. Preserve older tested-image
records. Repeat only the checks named by that review; never rewrite an old
observation as if it ran on the new image.

## 2. Record the board, operator, and fixture

Record a stable board serial/asset ID, visible hardware revision, operator
identity, and UTC timestamp. A host COM port is transient and is not a board
identity.

Give the fixture a stable ID and check its schematic or wiring record into
`docs/evidence/phase2-hardware/`. The SEN0305 Board Port defines the shared
external connector routes as:

| Mode | Signal | K210 IO |
|---|---|---:|
| UART | RX | 34 |
| UART | TX | 35 |
| I2C | SCL | 34 |
| I2C | SDA | 35 |

For the UART portion, connect the external connector TX route to its RX route.
For I2C controller qualification, connect a deterministic target with a known
identity and 7-bit address. Record the address in both the fixture and I2C
result sections. Use the board vendor's physical connector pinout to identify
power and ground; the logical IO table above is not a power-pin diagram. Verify
compatible logic voltage, common ground, and suitable I2C pull-ups before
powering the fixture.

## 3. Flash the exact image

Flashing replaces the installed firmware and therefore requires operator
approval. Preserve USERFS: do not add `--erase`.

```powershell
python tools\hkflash.py flash-monitor `
  build\huskylens-sen0305\hackylens-full.bin `
  --board huskylens-sen0305 --port COM10 --duration 10
```

Replace `COM10` with the currently detected port. Save the complete preflight,
flash, boot, and HMPY HELLO output as a raw log. Confirm that the booted version,
board ID, and image candidate agree before starting observations.

## 4. Physical checks

Use UTC timestamps and a monotonic host/instrument clock in raw logs. Retain
the unprocessed samples; summaries alone are not evidence.

### Buttons and Time

Exercise Left, OK, Right, and Back separately. For each button capture press,
release, and a sustained hold. Verify one edge per accepted transition and zero
repeated edges during the hold. Record sample count plus min, p99, and max for
debounce latency and delivered-event latency.

Record the same statistics for monotonic-read overhead, bounded sleep latency,
and cooperative cancel latency. Confirm that the monotonic value never moves
backwards during the session.

### Display

Observe the menu and every native view touched by Phase 2. Exercise camera and
files full-frame presentation, Pong dirty frames, and the MicroPython overlay.
For MicroPython, cover success, cancel, retry, cleanup, and restoration of the
latest native screen.

Record full-present samples; every measured full present must be at most
`500000 us`. Compare the same instrumented workload with its candidate
baseline; regression must be at most `10%`. Save timing logs and screen/frame
captures needed to identify the exercised views.

### UART and I2C

With the UART loopback installed, transmit a non-trivial, byte-distinct payload
and verify exact received bytes. Observe completion only after the final byte
has drained onto the wire. Cancel a longer transfer and keep observing past the
deadline; record zero late bytes.

Move to the documented I2C target. Exercise controller write, read, and a
combined prefix/read transaction. Force one bounded NACK or equivalent fixture
error, then prove recovery. Switch UART to I2C and back without rebooting.

Run the normal external/HMPY service before MicroPython takes the connector.
After Python success, cancellation, and error cleanup, prove that the configured
native transport is restored and complete at least one HMPY request/response.

### Lights and regression

Observe backlight, illumination, and RGB. Exercise normal completion,
cancellation/error cleanup to safe-off, and restoration of persisted settings.

Across the retained physical ledger, cover boot, camera capture, SD
read/write/delete, Files decode and frame-pool reuse, settings persistence,
Sleep, and HMPY. The closure image itself must pass boot sanity and every check
affected by its impact review. Do not format USERFS as part of acceptance.

## 5. Evidence and validation

Create `docs/evidence/phase2-hardware-smoke.json` as canonical JSON (UTF-8,
sorted keys, two-space indentation, trailing newline). The validator defines
the exact field set and rejects unknown fields. Its eight required result IDs
are:

- `buttons`;
- `display`;
- `uart`;
- `i2c`;
- `external_service_restore`;
- `lights`;
- `time`;
- `regression`.

Every result must reference one or more declared artifacts. A carried result's
raw artifact must identify its original tested commit/image; the impact review
must explain why the closure diff does not affect it. The artifact list must
include at least one `fixture-schematic`, one `raw-log`, and one `timing-log`;
every path must stay under
`docs/evidence/phase2-hardware/` and its content SHA-256 must match.

Use [the validator](../tools/check_phase2_hardware.py) and its deterministic
negative fixtures as the schema reference:

```powershell
python tools\check_phase2_hardware.py `
  --evidence docs\evidence\phase2-hardware-smoke.json
python -m unittest -v tests.test_phase2_hardware
```

The release workflow automatically runs the hardware validator once the actual
hardware-smoke file exists. Keep Phase 2.13 `in_progress` until the evidence
commit itself is green. Only then may a separate documentation-only commit mark
all Phase 2.13 checklist items complete.

## Rollback and cleanup

Restore temporary settings and remove only files created for the session. Keep
candidate results, impact reviews, raw logs, timings, and fixture records
immutable. A later binary gets a new closure identity and targeted impact
review; it does not erase unrelated physical observations.
