# Dristy continuity

## Hardware

- Board profile: `huskylens-sen0305` (OV2640, USB serial `/dev/ttyUSB0`)
- Flash: `hkflash flash … --uploader-reset` (BootROM timeout → retry with uploader-reset)

## Stale dist trap

If `build_firmware.py` fails, **`dist/*.bin` may be old**. Always run:

```bash
python3 tools/check_build_freshness.py
```

Compare SHA256 after every successful build.

## Camera / modes

- Classical LCD modes use `CAMERA_RUNTIME_CLASSICAL` + pipeline `camera_stream`.
- `dristy_pipeline_tick()` skips classical work unless the camera is live and a new stream sequence arrived.
- Host `SET_MODE` schedules vision shell via `dristy_host_mode_service()` on the system tick.

## Research archive

Gate characterization and deep dives: [`huskylens/docs/`](../huskylens/docs/)  
Evidence JSON: [`huskylens/docs/evidence/`](../huskylens/docs/evidence/)

## Workspace path

Directory: **`Ros_workspaces/dristy_ws`** (renamed from `R_n_d-ws`, 2026-09-19). Update IDE and Docker bind mounts if they still reference the old name.
