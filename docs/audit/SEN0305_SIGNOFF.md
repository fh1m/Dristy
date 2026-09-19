# SEN0305 hardware sign-off (in progress)

| Check | Result | Firmware SHA |
|-------|--------|--------------|
| Build + freshness | PASS | `8659b639…` |
| Flash | PASS | 1,590,648 bytes |
| Boot Comassless credit | PASS (serial) | |
| `dristy_mode_stress.py` (2 cycles) | PASS | |
| `dristy_mode_smoke.py` full matrix | Intermittent UART timeouts after stress; re-run isolated | |

Next: menu LCD per-mode visual pass + smoke 18/18 isolated after reboot.

Recorded: 2026-09-19
