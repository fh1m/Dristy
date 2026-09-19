#!/usr/bin/env python3
"""Measure what the stock HUSKYLENS firmware actually delivers.

DFRobot markets "30 frames per second"; independent reports put it nearer
10-12. This separates the two numbers that matter and are usually conflated:

  * device frame rate  - how fast the camera pipeline itself advances. Taken
    from the frame_number field in RETURN_INFO, so it is independent of how
    fast we can poll.
  * poll rate          - how many complete request/response round trips the
    serial link sustains per second, with latency percentiles.

Read-only: it polls, it never learns, forgets, or changes the algorithm.

    python3 tools/hl_bench.py --port /dev/ttyUSB0 --baud 9600 --duration 15
"""

from __future__ import annotations

import argparse
import json
import statistics
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "src"))

from huskylens_proto import HuskyLens, SerialTransport  # noqa: E402
from huskylens_proto.client import ProtocolError  # noqa: E402

REQUESTS = {
    "request": lambda d: d.request(),
    "blocks": lambda d: d.request_blocks(),
    "arrows": lambda d: d.request_arrows(),
    "learned": lambda d: d.request_learned(),
}


def percentile(values: list[float], fraction: float) -> float:
    if not values:
        return float("nan")
    ordered = sorted(values)
    index = min(len(ordered) - 1, int(round(fraction * (len(ordered) - 1))))
    return ordered[index]


def run(device: HuskyLens, kind: str, duration: float) -> dict:
    call = REQUESTS[kind]
    latencies: list[float] = []
    errors = 0
    frame_first: int | None = None
    frame_last: int | None = None
    frame_wraps = 0
    result_counts: list[int] = []

    start = time.monotonic()
    deadline = start + duration
    while time.monotonic() < deadline:
        t0 = time.monotonic()
        try:
            results = call(device)
        except ProtocolError:
            errors += 1
            continue
        latencies.append(time.monotonic() - t0)
        result_counts.append(results.info.result_count)

        frame = results.info.frame_number
        if frame_first is None:
            frame_first = frame
        elif frame_last is not None and frame < frame_last:
            # frame_number is a uint16 and wraps.
            frame_wraps += 1
        frame_last = frame
    elapsed = time.monotonic() - start

    advanced = None
    if frame_first is not None and frame_last is not None:
        advanced = (frame_last - frame_first) + frame_wraps * 0x10000

    return {
        "request": kind,
        "elapsed_s": round(elapsed, 3),
        "polls": len(latencies),
        "errors": errors,
        "poll_rate_hz": round(len(latencies) / elapsed, 2) if elapsed else None,
        "device_fps": round(advanced / elapsed, 2)
        if advanced is not None and elapsed
        else None,
        "frames_advanced": advanced,
        "latency_ms": {
            "min": round(min(latencies) * 1000, 2) if latencies else None,
            "mean": round(statistics.fmean(latencies) * 1000, 2) if latencies else None,
            "p50": round(percentile(latencies, 0.50) * 1000, 2),
            "p95": round(percentile(latencies, 0.95) * 1000, 2),
            "max": round(max(latencies) * 1000, 2) if latencies else None,
        },
        "results_per_poll": {
            "min": min(result_counts) if result_counts else None,
            "mean": round(statistics.fmean(result_counts), 2) if result_counts else None,
            "max": max(result_counts) if result_counts else None,
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyUSB0")
    parser.add_argument("--baud", type=int, required=True)
    parser.add_argument("--duration", type=float, default=10.0, help="seconds per test")
    parser.add_argument("--requests", nargs="*", default=list(REQUESTS), choices=list(REQUESTS))
    parser.add_argument("--note", default="", help="e.g. the algorithm shown on screen")
    parser.add_argument("--json", type=Path, default=Path("artifacts/benchmark.json"))
    args = parser.parse_args()

    print(
        f"Benchmarking {args.port} at {args.baud} baud, "
        f"{args.duration:g}s per request type.\n"
        "Leave the scene in front of the camera unchanged for comparable numbers.\n"
    )

    report = {
        "port": args.port,
        "baud": args.baud,
        "note": args.note,
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "tests": [],
    }

    with SerialTransport(port=args.port, baudrate=args.baud) as transport:
        device = HuskyLens(transport)
        if not device.knock():
            print("Device did not answer a KNOCK; aborting.", file=sys.stderr)
            return 1
        for kind in args.requests:
            print(f"  {kind} ...", end="", flush=True)
            result = run(device, kind, args.duration)
            report["tests"].append(result)
            print(
                f" poll {result['poll_rate_hz']} Hz, "
                f"device {result['device_fps']} fps, "
                f"p50 {result['latency_ms']['p50']} ms, "
                f"p95 {result['latency_ms']['p95']} ms, "
                f"{result['errors']} error(s)"
            )

    args.json.parent.mkdir(parents=True, exist_ok=True)
    args.json.write_text(json.dumps(report, indent=2) + "\n")
    print(f"\nResults written to {args.json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
