# Agent onboarding — Dristy

## Mission

Ship and maintain **Dristy** firmware + host API on live K210 hardware. Do not
trust menu reports alone — run smoke/stress on `/dev/ttyUSB0`.

## Read first

1. [`README.md`](README.md) — what the product is (live LCD evidence)
2. [`docs/CONTINUITY.md`](docs/CONTINUITY.md)
3. [`docs/DRISTY_INVARIANTS.md`](docs/DRISTY_INVARIANTS.md)
4. [`dristy/docs/DRISTY_ARCHITECTURE.md`](dristy/docs/DRISTY_ARCHITECTURE.md)

## Verify loop

```bash
python3 dristy/firmware/k210/tools/build_firmware.py full --board sen0305
python3 dristy/firmware/k210/tools/check_build_freshness.py
sudo python3 dristy/firmware/k210/tools/dristy_mode_smoke.py --port /dev/ttyUSB0
sudo python3 dristy/firmware/k210/tools/dristy_mode_stress.py --port /dev/ttyUSB0
```

Pull an LCD dump the same way the README gallery was made:

```bash
sudo python3 dristy/firmware/k210/tools/hkflash.py screenshot \
  --board sen0305 --port /dev/ttyUSB0 --timeout 60 -o /tmp/lcd.bmp
```

## Credits

Comassless — Muhammad Fahim Faisal, Rakibul Islam
