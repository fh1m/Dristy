# WDT1 hardware acceptance

This procedure verifies the fatal MicroPython STOP fallback on a real K210.
The production Python API intentionally has no operation that can wedge core 1,
so the trigger uses a clearly marked, test-only firmware build. The ordinary
build always compiles the fault-injection branch out.

Current status: **NOT RUN**. This document is the acceptance procedure, not
evidence that the physical WDT1 reset/recovery gate has passed.

## Safety and evidence

- Flashing replaces the installed firmware and requires explicit operator
  approval. Do not add `--erase`; the normal image write preserves userfs.
- The test build reports version `<VERSION>-wdtfi`. Never distribute it as a
  release image: every MicroPython STOP or deadline deliberately wedges core 1.
- Record the original firmware/autostart/startup selection before the test.
- A passing gate consists of both the observed physical reset and the two JSON
  reports below. The postcheck alone does not prove how the reset was caused.

## 1. Build and install the test image

From the repository root:

```powershell
python tools\build_firmware.py full --board huskylens-sen0305 --wdt-fault-injection
python tools\hkflash.py flash-monitor build\huskylens-sen0305\hackylens-full-wdtfi.bin --board huskylens-sen0305 --port COM10 --duration 10 --allow-missing-sidecar
```

Create a unique fixture and select it as startup. First note the previous
startup so it can be restored afterwards.

```powershell
@'
print("WDT_READY")
while True:
    pass
'@ | Set-Content -Encoding ascii build\hka-wdt-fi.py
python tools\hkpy.py --port COM10 upload build\hka-wdt-fi.py --name hka-wdt-fi.py --startup
```

On the device set **Settings / Autostart / MicroPython**. This proves that a
WDT recovery boot suppresses firmware autostart of the app. The native app does
not execute startup metadata on entry; the fixture is triggered explicitly in
the next section.

Capture the immutable pre-reset evidence. This invocation is read-only:

```powershell
$wdtVersion = ((Get-Content VERSION -Raw).Trim() + "-wdtfi")
python tools\hmpy_acceptance.py --port COM10 --expected-version $wdtVersion `
  --report build\hardware-acceptance\wdt-before.json
```

The report must be `PASS`, must show `boot_flags=0`, and must name
`hka-wdt-fi.py` as startup.

## 2. Trigger the physical WDT1 reset

Start the fixture explicitly, wait until `WDT_READY` is visible in the device
log, then request STOP:

```powershell
python tools\hkpy.py --port COM10 run hka-wdt-fi.py --time-limit-ms 30000
python tools\hkpy.py --port COM10 monitor --seconds 2
python tools\hkpy.py --port COM10 stop
```

The bounded monitor must contain `WDT_READY`; otherwise do not send STOP and do
not count the attempt as trigger evidence.

The test-only VM hook now spins on core 1. After the two-second STOP grace,
core 0 arms WDT1 and waits; the K210 then physically resets. Record the serial
disconnect/reconnect or boot banner as trigger evidence. Do not power-cycle or
press RESET during this interval because that would replace the WDT reset
cause.

## 3. Verify the recovery boot read-only

After the port reconnects, run:

```powershell
python tools\hmpy_acceptance.py --port COM10 --expected-version $wdtVersion `
  --verify-wdt-recovery `
  --wdt-baseline build\hardware-acceptance\wdt-before.json `
  --report build\hardware-acceptance\wdt-after.json
```

The postcheck requires `WDT1_RECOVERY`, `STOPPED / EXIT_NONE / run_id=0`, the
same firmware/board/files/startup as the baseline, and three seconds with no
run. It rejects FORMAT, workflow, or reconnect suites so this phase cannot
mutate the recovery evidence.

Finally restore the previous Autostart and startup selections, delete only the
test fixture, rebuild without `--wdt-fault-injection`, and flash the normal
image. A production build must report the canonical version without
`-wdtfi`.
