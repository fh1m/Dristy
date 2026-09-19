#!/usr/bin/env python3
"""Bring up the HUSKYLENS link and report what the device is.

Scans candidate baud rates for a KNOCK response, then reports model, firmware
payload and a sample of live results. Strictly read-only: it never sends a
command that changes device state.

    python3 tools/hl_probe.py --port /dev/ttyUSB0
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "src"))

from huskylens_proto import (  # noqa: E402
    CANDIDATE_BAUDS,
    Command,
    HuskyLens,
    SerialTransport,
    TransportError,
    find_ports,
)
from huskylens_proto.client import ProtocolError  # noqa: E402


def scan_bauds(port: str, bauds: tuple[int, ...], attempts: int) -> list[dict]:
    results = []
    for baud in bauds:
        record = {"baud": baud, "knock": False, "raw": ""}
        try:
            with SerialTransport(port=port, baudrate=baud, timeout=0.4) as transport:
                for _ in range(attempts):
                    frames = transport.exchange(Command.REQUEST_KNOCK, timeout=0.4)
                    if frames:
                        record["raw"] = ",".join(
                            f"0x{f.command:02x}" for f in frames
                        )
                        record["knock"] = any(
                            f.command == Command.RETURN_OK for f in frames
                        )
                        break
                    time.sleep(0.05)
        except TransportError as exc:
            record["error"] = str(exc)
        results.append(record)
        status = "OK" if record["knock"] else (record.get("error") or "silent")
        print(f"  {baud:>8} baud : {status}")
    return results


def describe(device: HuskyLens) -> dict:
    report: dict = {}

    try:
        report["is_pro"] = device.is_pro()
    except ProtocolError as exc:
        report["is_pro_error"] = str(exc)

    try:
        payload = device.firmware_version()
        report["firmware_version_raw"] = payload.hex(" ") if payload else None
    except ProtocolError as exc:
        report["firmware_version_error"] = str(exc)

    try:
        results = device.request()
        report["sample"] = {
            "result_count": results.info.result_count,
            "learned_count": results.info.learned_count,
            "frame_number": results.info.frame_number,
            "blocks": [vars(b) for b in results.blocks],
            "arrows": [vars(a) for a in results.arrows],
        }
    except ProtocolError as exc:
        report["sample_error"] = str(exc)

    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=None, help="serial device, e.g. /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=None, help="skip the scan")
    parser.add_argument("--attempts", type=int, default=3)
    parser.add_argument("--json", type=Path, default=None, help="write report here")
    args = parser.parse_args()

    port = args.port
    if port is None:
        detected = find_ports()
        if not detected:
            print("No HUSKYLENS-compatible USB-UART bridge found.", file=sys.stderr)
            return 2
        port = detected[0]
        print(f"Auto-detected port: {port}")

    bauds = (args.baud,) if args.baud else CANDIDATE_BAUDS
    print(f"Scanning {len(bauds)} baud rate(s) on {port} for a KNOCK response:")
    scan = scan_bauds(port, bauds, args.attempts)

    live = [r for r in scan if r["knock"]]
    report = {"port": port, "scan": scan, "linked": bool(live)}

    if not live:
        print(
            "\nNo response at any baud rate.\n"
            "The most likely cause is that the device's Protocol Type is set to\n"
            "I2C. On the HUSKYLENS, open General Settings -> Protocol Type and\n"
            "select a Serial rate, then run this again.",
            file=sys.stderr,
        )
    else:
        baud = live[0]["baud"]
        report["baud"] = baud
        print(f"\nLink established at {baud} baud. Querying device:")
        with SerialTransport(port=port, baudrate=baud) as transport:
            report["device"] = describe(HuskyLens(transport))
        print(json.dumps(report["device"], indent=2))

    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(report, indent=2) + "\n")
        print(f"\nReport written to {args.json}")

    return 0 if live else 1


if __name__ == "__main__":
    raise SystemExit(main())
