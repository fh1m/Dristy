# Dristy workspace

Comassless vision stack for Kendryte K210 camera modules (DFRobot SEN0305 class
hardware). Firmware, Python host API, flash tools, and research live here so the
next developer or agent can continue without rediscovering hardware traps.

## Authors

Muhammad Fahim Faisal and Rakibul Islam — **Comassless**

## Layout

| Path | Purpose |
|------|---------|
| [`huskylens/firmware/hackylens/`](huskylens/firmware/hackylens/) | K210 firmware build, flash, SD staging |
| [`huskylens/dristy-py/`](huskylens/dristy-py/) | Host DLP Python API |
| [`huskylens/docs/`](huskylens/docs/) | Architecture, gates, research index |
| [`docs/`](docs/) | Workspace continuity and invariants |
| [`AGENTS.md`](AGENTS.md) | Agent onboarding |
| [`docs/CONTINUITY.md`](docs/CONTINUITY.md) | Flash, verify, known failures |

## Quick start (hardware)

```bash
cd huskylens/firmware/hackylens
python3 tools/build_firmware.py full --board huskylens-sen0305
python3 tools/check_build_freshness.py
sudo python3 tools/hkflash.py flash dist/hackylens-full-huskylens-sen0305.bin \
  --board huskylens-sen0305 --port /dev/ttyUSB0 --uploader-reset
sudo python3 tools/dristy_mode_smoke.py --port /dev/ttyUSB0 --run-id verify
sudo python3 tools/dristy_mode_stress.py --port /dev/ttyUSB0
```

Rename this folder to **`dristy_ws`** when integrating with your host layout
(docker bind mounts, IDE workspace).

## License

MIT — see [LICENSE](LICENSE).
