#!/usr/bin/env python3
"""Sweep the SEN0305 command space hunting for undocumented commands.

The published protocol names commands 0x20-0x3E but leaves gaps (0x31, 0x38,
0x3A) and documents 0x3C by name only. This probes a configurable range and
records every response, so we learn what the stock firmware actually answers
rather than what the document claims.

Safety: commands that change learned models, SD card contents, the active
algorithm or the screen overlay are refused outright. There is no override.

    python3 tools/hl_sweep.py --port /dev/ttyUSB0 --baud 9600
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "src"))

from dlp_proto import MUTATING_COMMANDS, Command, SerialTransport  # noqa: E402

#: Known response commands, used to label what came back.
RESPONSE_NAMES = {int(c): c.name for c in Command}

#: Refused unconditionally. MUTATING_COMMANDS covers the documented ones; the
#: undocumented neighbours of LEARN/FORGET are excluded too, because an unknown
#: command sitting next to FORGET is not worth the risk of wiping learned data.
BLOCKED = {int(c) for c in MUTATING_COMMANDS} | {0x31, 0x38}


def sweep(
    transport: SerialTransport,
    low: int,
    high: int,
    payloads: list[bytes],
    settle: float,
) -> list[dict]:
    findings = []
    for command in range(low, high + 1):
        if command in BLOCKED:
            findings.append(
                {
                    "command": f"0x{command:02x}",
                    "skipped": "blocked: mutates device state or is adjacent to a destructive command",
                }
            )
            print(f"  0x{command:02x}  SKIPPED (blocked)")
            continue

        for payload in payloads:
            transport.flush()
            frames = transport.exchange(command, payload, timeout=0.4)
            record = {
                "command": f"0x{command:02x}",
                "payload": payload.hex(" ") or "(none)",
                "response_count": len(frames),
                "responses": [
                    {
                        "command": f"0x{f.command:02x}",
                        "name": RESPONSE_NAMES.get(f.command, "UNKNOWN"),
                        "data": f.data.hex(" "),
                    }
                    for f in frames
                ],
            }
            findings.append(record)

            if frames:
                names = ", ".join(r["name"] for r in record["responses"])
                documented = command in RESPONSE_NAMES
                marker = " " if documented else "  <-- UNDOCUMENTED"
                print(
                    f"  0x{command:02x}  payload={record['payload']:<12} "
                    f"-> {len(frames)} frame(s): {names}{marker}"
                )
            time.sleep(settle)
    return findings


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyUSB0")
    parser.add_argument("--baud", type=int, required=True)
    parser.add_argument("--low", type=lambda v: int(v, 0), default=0x1F)
    parser.add_argument("--high", type=lambda v: int(v, 0), default=0x5F)
    parser.add_argument("--settle", type=float, default=0.08)
    parser.add_argument(
        "--json", type=Path, default=Path("artifacts/command_sweep.json")
    )
    args = parser.parse_args()

    # Try an empty payload and a two-byte one, since several commands take a
    # uint16 argument and may stay silent without it.
    payloads = [b"", b"\x01\x00"]

    print(
        f"Sweeping 0x{args.low:02x}-0x{args.high:02x} on {args.port} at {args.baud} baud.\n"
        f"{len(BLOCKED)} command(s) are blocked as unsafe.\n"
    )

    with SerialTransport(port=args.port, baudrate=args.baud, timeout=0.4) as transport:
        findings = sweep(transport, args.low, args.high, payloads, args.settle)

    answered = [f for f in findings if f.get("response_count")]
    undocumented = [
        f
        for f in answered
        if int(f["command"], 16) not in RESPONSE_NAMES
    ]

    args.json.parent.mkdir(parents=True, exist_ok=True)
    args.json.write_text(
        json.dumps(
            {
                "port": args.port,
                "baud": args.baud,
                "range": [f"0x{args.low:02x}", f"0x{args.high:02x}"],
                "blocked": sorted(f"0x{c:02x}" for c in BLOCKED),
                "findings": findings,
            },
            indent=2,
        )
        + "\n"
    )

    print(
        f"\n{len(answered)} probe(s) got a response; "
        f"{len(undocumented)} came from commands the protocol document does not name."
    )
    print(f"Full log written to {args.json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
