# Dristy claims matrix (SEN0305)

Maps [DRISTY_ARCHITECTURE.md](DRISTY_ARCHITECTURE.md) claims to implementation and hardware test IDs.

**Baseline (pre-fix, USB `/dev/ttyUSB0`, Dristy 0.4.0 + Dristy menu only):**

| Mode | fps (log) | present_us | compose_us | Notes |
|------|-----------|------------|------------|--------|
| Camera preview | ~25.5 | ~31200 | ~6590 | Full-frame SPI present dominates |
| Object detect | ~7–8 | similar | + KPU in display consumer | Inference tied to preview tick |
| Face / AprilTag | low | similar | classical + present | Same pattern |

**Acceptance targets (post-fix):**

| Mode | Min fps | present_us p99 |
|------|---------|------------------|
| Camera | 20 | 25000 |
| Object / face / tag UI | 12 preview | 25000 |

---

## §0 Identity

| Claim | Source | Runtime hook | Host API | HW test |
|-------|--------|--------------|----------|---------|
| Name "Dristy" | ARCH §0 | boot log, IDENTIFY | `Dristy.identify()` | HW-03 `[BOOT] Dristy` |
| Boot splash | ARCH §10 | `dristy_boot_view_show` | — | HW-04 HKSHOT slate/amber |

## §1 Principles

| Claim | Source | Runtime hook | Host API | HW test |
|-------|--------|--------------|----------|---------|
| Mailbox frames | ARCH §1 | `camera_service` / pipeline | — | HW-06 camera fps |
| LCD off robotics | ARCH §1 | `dristy_lcd` + DLP 0x70 | `lcd_off()` | HW-05 doctor |

## §3 Pipeline

| Claim | Source | Runtime hook | Host API | HW test |
|-------|--------|--------------|----------|---------|
| KPU detect + track | ARCH §3 | `dristy_pipeline` + postprocess | `set_mode`, `read()` | HW-05, HW-06 |
| Classical tags/QR | ARCH §3 | `process_classical` / app bridge | `read_tags()` | HW-06 tag mode |
| Result bus | ARCH §3 | `dristy_result_bus` | `CMD_GET_RESULT` | HW-05 |

## §10 DLP

| Claim | Source | Runtime hook | Host API | HW test |
|-------|--------|--------------|----------|---------|
| 0x55 0xAA framing | ARCH §10 | `dristy_uart_bridge` | `protocol.py` | HW-05 |
| DLP 0x40–0x7F | ARCH §10 | `dristy_protocol.c` | `core.py` | HW-05 |
| Stock 0x20–0x3E | ARCH §10 | `dristy_legacy_compat` | `hl_probe` | HW-05 knock |
| HK `H`/`K` frames | Dristy | `external_link_service` | HK tools | HW-03 HKHELP |

## §10 Boot

| Claim | Source | Runtime hook | Host API | HW test |
|-------|--------|--------------|----------|---------|
| PLL1 400 MHz | ARCH §10 | `dristy_boot` | `GET_PERF` kpu_mhz | HW-05 |
| Dristy version | ARCH §10 | `DRISTY_VERSION` | IDENTIFY | HW-03 |

## Host dev workflow

```text
# After flash (SEN0305):
pip install -e "dristy/dristy-py[viewer]"
dristy-viewer --port /dev/ttyUSB0 --mode detect_track

# Edit firmware / modes → rebuild → flash → same viewer
python dristy/firmware/dristy/tools/build_firmware.py full --board sen0305
sudo python dristy/firmware/dristy/tools/hkflash.py flash dist/... --board sen0305 --port /dev/ttyUSB0
```

## Menu capability launcher

| Claim | Runtime hook | HW test |
|-------|--------------|---------|
| Mode-first menu (25 modes + tools) | `dristy_mode_menu.c`, `vision_mode_app` | HW-04 scroll all sections |
| STUB modes | `dristy_mode_readiness.c` | Enter shows not wired |
| Host stream toggle | Settings → Host stream, `dristy_uart_bridge_set_ctrl_stream` | DLP ctrl stream |

## UI

| Claim | Source | Runtime hook | Host API | HW test |
|-------|--------|--------------|----------|---------|
| Scrollable list launcher | UI plan | `ui_menu.c`, `hk_menu.c`, `dristy_mode_menu.c` | — | HW-04 |
| Menu button legend | boot `[BTN]` | `hk_dispatch.c`, `dristy_ui` footer pills | — | HW-03 |
| Dristy boot LCD (no Dristy 1bpp) | UI plan | `display_binding` → `dristy_boot_view_show` | — | HW-04 |
| In-app chrome | Plan P5 | `dristy_ui` app header/footer + migrated views | — | HW-04 |

## Test IDs

- **HW-03**: `dristy_hw_verify` serial monitor 15s
- **HW-04**: HKSHOT menu BMP (no terminal-green flood)
- **HW-05**: `dristy_doctor.py` IDENTIFY, mode, LCD
- **HW-06**: Per-mode `[CAM] fps=` / app debug lines

Evidence file: `docs/evidence/dristy-sen0305-smoke.json` (after final flash).
