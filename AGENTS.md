# Agent onboarding — Dristy

## Mission

Ship and maintain **Dristy** firmware + host API on live K210 hardware. Do not
trust menu reports alone — run smoke/stress on `/dev/ttyUSB0`.

## Read first

1. [`docs/CONTINUITY.md`](docs/CONTINUITY.md)
2. [`docs/DRISTY_INVARIANTS.md`](docs/DRISTY_INVARIANTS.md)
3. [`huskylens/docs/DRISTY_ARCHITECTURE.md`](huskylens/docs/DRISTY_ARCHITECTURE.md)

## Verify loop

```bash
python3 huskylens/firmware/hackylens/tools/build_firmware.py full --board huskylens-sen0305
python3 huskylens/firmware/hackylens/tools/check_build_freshness.py
sudo python3 huskylens/firmware/hackylens/tools/dristy_mode_smoke.py --port /dev/ttyUSB0
sudo python3 huskylens/firmware/hackylens/tools/dristy_mode_stress.py --port /dev/ttyUSB0
```

## Credits

Comassless — Muhammad Fahim Faisal, Rakibul Islam
