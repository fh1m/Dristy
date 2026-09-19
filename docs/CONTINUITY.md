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

## Workspace rename

GitHub repo target: **`fh1m/Dristy`**. Rename host directory `R_n_d-ws` → `dristy_ws` and update docker/IDE paths accordingly.
