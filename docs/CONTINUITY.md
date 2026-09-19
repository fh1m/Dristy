# Dristy continuity

## Hardware

- Board profile: `sen0305` (OV2640, USB serial `/dev/ttyUSB0`)
- Flash: `hkflash flash … --uploader-reset` (BootROM timeout → retry with uploader-reset)
- Opening the USB serial with DTR/RTS toggling **resets the K210**. Screenshot/doctor hold lines low.

## Stale dist trap

If `build_firmware.py` fails, **`dist/*.bin` may be old**. Always run:

```bash
python3 dristy/firmware/k210/tools/check_build_freshness.py
```

Compare SHA256 after every successful build.

## Camera / modes

- Classical LCD modes use `CAMERA_RUNTIME_CLASSICAL` + pipeline `camera_stream`.
- `dristy_pipeline_tick()` skips classical work unless the camera is live and a new stream sequence arrived.
- Host `SET_MODE` schedules vision shell via `dristy_host_mode_service()` on the system tick.
- One DVP owner: do not run two camera apps at once.

## UART screenshots

Firmware prints `[SHOT] UART command HKSHOT…` at boot. The debug RX task starts **after** the boot splash, so `HKSHOT` captures menu/apps, not the 1.8 s logo. Wait for `[SHELL] screen MENU` before the first shot.

## Research archive

Gate characterization: [`dristy/docs/`](../dristy/docs/)  
Evidence JSON: [`dristy/docs/evidence/`](../dristy/docs/evidence/)  
Live LCD PNGs: [`docs/media/live/`](media/live/)

## Workspace path

Directory: **`Ros_workspaces/dristy_ws`**. GitHub: [fh1m/Dristy](https://github.com/fh1m/Dristy).
