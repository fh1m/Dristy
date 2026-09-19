#!/usr/bin/env python3
"""
Dristy Benchmark Runner
========================

Host-side tool to:
  1. Enable on-device profiling via DLP
  2. Run for N seconds collecting data
  3. Retrieve benchmark report
  4. Display results + theoretical ceiling comparison
  5. Generate optimization recommendations

Usage:
  python3 dristy_bench_runner.py --port /dev/ttyUSB0 --duration 10
  python3 dristy_bench_runner.py --port /dev/ttyUSB0 --mode detect_track --duration 30
  python3 dristy_bench_runner.py --theoretical  # show theoretical ceilings only
"""

import argparse
import struct
import sys
import time

# Stage names (must match dristy_bench.h enum order)
STAGE_NAMES = [
    "FRAME_TOTAL",
    "DVP_CAPTURE",
    "KPU_INFERENCE",
    "YOLO_DECODE",
    "NMS",
    "TRACKER",
    "APRILTAG",
    "ARUCO",
    "MOTION",
    "FLOW",
    "COLOUR",
    "QR",
    "POSE",
    "TARGET_SELECT",
    "RESULT_BUS",
    "DLP_SERIALIZE",
    "LCD_UPDATE",
    "IDLE",
]

# DLP protocol constants
DLP_SYNC = bytes([0x55, 0xAA])
DLP_ADDR = 0x11

# DLP command IDs
DLP_CMD_BENCH_ENABLE = 0x68
DLP_CMD_BENCH_REPORT = 0x69
DLP_CMD_BENCH_RESET = 0x6A
DLP_CMD_BENCH_PRINT = 0x6B
DLP_CMD_SET_MODE = 0x40
DLP_RET_OK = 0x2E
DLP_RET_PERF = 0x54


def build_dlp_packet(cmd, data=b""):
    """Build a DLP wire packet."""
    payload = bytes([DLP_ADDR, len(data), cmd]) + data
    checksum = sum(payload) & 0xFF
    return DLP_SYNC + payload + bytes([checksum])


def parse_dlp_response(data):
    """Parse a DLP response, return (cmd, payload) or None."""
    if len(data) < 6:
        return None
    idx = data.find(b"\x55\xaa")
    if idx < 0:
        return None
    if idx + 5 > len(data):
        return None
    addr = data[idx + 2]
    length = data[idx + 3]
    cmd = data[idx + 4]
    if idx + 5 + length > len(data):
        return None
    payload = data[idx + 5 : idx + 5 + length]
    return (cmd, payload)


def parse_bench_report(payload):
    """Parse serialised benchmark report from device."""
    if len(payload) < 8:
        return None

    report = {}
    report["frames"] = struct.unpack_from("<H", payload, 2)[0]
    report["fps_x10"] = struct.unpack_from("<H", payload, 4)[0]
    report["kpu_util_x10"] = struct.unpack_from("<H", payload, 6)[0]

    stages = []
    off = 8
    for i, name in enumerate(STAGE_NAMES):
        if off + 8 > len(payload):
            break
        mean, max_v, p95, count = struct.unpack_from("<HHHH", payload, off)
        stages.append({
            "name": name,
            "mean_us": mean,
            "max_us": max_v,
            "p95_us": p95,
            "count": count,
        })
        off += 8

    report["stages"] = stages
    return report


# ---------------------------------------------------------------------------
# Theoretical Ceiling Analysis
# ---------------------------------------------------------------------------

THEORETICAL = {
    "CPU_MHz": 400,
    "CPU_MHz_OC": 600,
    "KPU_TOPS": 0.8,
    "SRAM_total_KB": 6144,
    "AI_SRAM_KB": 2048,
    "DVP_max_FPS": 60,
    "KPU_max_input": "320×256",
    "LCD_refresh_ms": 8,
    "UART1_max_baud": 921600,
    "HW_FFT_512pt_us": 16,
    "Flash_total_MB": 16,
}

# Expected timing per stage (µs) based on K210 hardware analysis
EXPECTED_TIMINGS = {
    "DVP_CAPTURE": {
        "note": "OV2640 320×240 QQVGA. DVP writes to SRAM via DMA.",
        "best_us": 6000,
        "typical_us": 16000,
        "worst_us": 33333,
        "budget_pct": 48,
    },
    "KPU_INFERENCE": {
        "note": "MobileNet-YOLOv2, 320×256×3 input. 5 depthwise-sep blocks.",
        "best_us": 28000,
        "typical_us": 40000,
        "worst_us": 55000,
        "budget_pct": 100,
        "optimization": [
            "Reduce input to 224×224 → ~25% faster",
            "Use kmodel v4 (nncase v0.2.0b4) for SW+HW mixed ops",
            "Quantize to int8 → better KPU utilisation",
            "Overclock CPU 400→600 MHz → ~20% faster post-processing",
        ],
    },
    "YOLO_DECODE": {
        "note": "Decode 10×8 grid × 5 anchors × 20 classes = 400 candidates.",
        "best_us": 800,
        "typical_us": 2000,
        "worst_us": 5000,
        "budget_pct": 6,
        "optimization": [
            "Inline exp() with lookup table → 3× faster sigmoid",
            "Skip classes with score < 0.01 early",
            "Fixed-point decode (Q8) eliminates float overhead",
        ],
    },
    "NMS": {
        "note": "Soft-NMS (Gaussian) over surviving candidates.",
        "best_us": 50,
        "typical_us": 200,
        "worst_us": 500,
        "budget_pct": 1,
    },
    "TRACKER": {
        "note": "DeepSORT: predict (Kalman) + associate (Hungarian) + update.",
        "best_us": 100,
        "typical_us": 800,
        "worst_us": 2000,
        "budget_pct": 2,
        "optimization": [
            "Fixed-point Kalman (Q12/Q20) → no float ops on predict",
            "Hungarian O(n³) is fast for n≤20 tracks",
            "Skip Mahalanobis gate for tentative tracks",
        ],
    },
    "APRILTAG": {
        "note": "Full detector at 160×120 grayscale (downsampled 2×).",
        "best_us": 6000,
        "typical_us": 12000,
        "worst_us": 20000,
        "budget_pct": 36,
        "optimization": [
            "Run on core 1 while KPU runs on core 0",
            "Reduce to 80×60 for faster quad detection",
            "Limit max detections to 4 → early exit",
        ],
    },
    "ARUCO": {
        "note": "Adaptive threshold + quad extraction + bit decode at 320×240.",
        "best_us": 5000,
        "typical_us": 10000,
        "worst_us": 18000,
        "budget_pct": 30,
        "optimization": [
            "Process at 160×120 (same as AprilTag)",
            "Use integral image for O(1) adaptive threshold",
            "Pre-filter quads by perimeter before bit extraction",
        ],
    },
    "MOTION": {
        "note": "Frame diff + morphology at 160×120 quarter-res.",
        "best_us": 1000,
        "typical_us": 2000,
        "worst_us": 3000,
        "budget_pct": 6,
    },
    "FLOW": {
        "note": "8 ROIs × HW FFT phase correlation. Mostly DMA-bound.",
        "best_us": 200,
        "typical_us": 500,
        "worst_us": 1000,
        "budget_pct": 2,
    },
    "POSE": {
        "note": "PnP via homography decomposition. ~3ms per tag.",
        "best_us": 500,
        "typical_us": 2000,
        "worst_us": 5000,
        "budget_pct": 6,
    },
    "TARGET_SELECT": {
        "note": "Scan tracks/tags/blobs for best primary target.",
        "best_us": 5,
        "typical_us": 20,
        "worst_us": 50,
        "budget_pct": 0,
    },
    "RESULT_BUS": {
        "note": "memset + atomic pointer swap.",
        "best_us": 2,
        "typical_us": 5,
        "worst_us": 10,
        "budget_pct": 0,
    },
    "LCD_UPDATE": {
        "note": "320×240×2 bytes over SPI0 @ 40 MHz. DMA-driven.",
        "best_us": 7500,
        "typical_us": 8000,
        "worst_us": 9000,
        "budget_pct": 24,
        "optimization": [
            "Skip LCD update when headless (drone mode) → save 8ms/frame",
            "Partial update: only redraw changed region",
            "Reduce to 30 Hz LCD while KPU runs at 30 Hz → interleave",
        ],
    },
}

# Pipeline configurations and their theoretical FPS
MODE_CEILINGS = {
    "DETECT_TRACK": {
        "stages": ["DVP_CAPTURE", "KPU_INFERENCE", "YOLO_DECODE", "NMS",
                    "TRACKER", "TARGET_SELECT", "RESULT_BUS", "LCD_UPDATE"],
        "pipelined": True,
        "note": "KPU on core 0, post-process on core 1. DVP overlaps KPU.",
    },
    "APRILTAG": {
        "stages": ["DVP_CAPTURE", "APRILTAG", "POSE", "TARGET_SELECT",
                    "RESULT_BUS", "LCD_UPDATE"],
        "pipelined": False,
        "note": "CPU-only. Can run on core 1 while core 0 handles DLP.",
    },
    "ARUCO": {
        "stages": ["DVP_CAPTURE", "ARUCO", "POSE", "TARGET_SELECT",
                    "RESULT_BUS", "LCD_UPDATE"],
        "pipelined": False,
    },
    "LANDING_TARGET": {
        "stages": ["DVP_CAPTURE", "APRILTAG", "POSE", "FLOW",
                    "TARGET_SELECT", "RESULT_BUS", "LCD_UPDATE"],
        "pipelined": False,
        "note": "AprilTag + optical flow for precision landing.",
    },
    "DETECT_TRACK_HEADLESS": {
        "stages": ["DVP_CAPTURE", "KPU_INFERENCE", "YOLO_DECODE", "NMS",
                    "TRACKER", "TARGET_SELECT", "RESULT_BUS"],
        "pipelined": True,
        "note": "No LCD → pure drone mode. Maximum FPS.",
    },
    "MOTION_GATED_DETECT": {
        "stages": ["DVP_CAPTURE", "MOTION", "KPU_INFERENCE", "YOLO_DECODE",
                    "NMS", "TRACKER", "RESULT_BUS"],
        "pipelined": True,
        "note": "KPU only fires when motion detected. Saves power.",
    },
}


def print_theoretical():
    """Print theoretical analysis without a device."""
    print("\n╔══════════════════════════════════════════════════════════════╗")
    print("║         DRISTY THEORETICAL PERFORMANCE CEILINGS             ║")
    print("╠══════════════════════════════════════════════════════════════╣")

    print("\n--- K210 Hardware Specs ---")
    for k, v in THEORETICAL.items():
        print(f"  {k:25s}: {v}")

    print("\n--- Per-Stage Expected Timings ---")
    print(f"  {'Stage':<18s} {'Best':>7s} {'Typical':>8s} {'Worst':>7s}  Notes")
    print(f"  {'─'*18} {'─'*7} {'─'*8} {'─'*7}  {'─'*30}")

    for name, info in EXPECTED_TIMINGS.items():
        print(f"  {name:<18s} {info['best_us']:>6d}µ {info['typical_us']:>7d}µ "
              f"{info['worst_us']:>6d}µ  {info.get('note', '')[:45]}")

    print("\n--- Mode FPS Ceilings ---")
    for mode_name, mode_cfg in MODE_CEILINGS.items():
        stages = mode_cfg["stages"]
        pipelined = mode_cfg.get("pipelined", False)

        if pipelined:
            # Pipelined: FPS = 1 / max(stage_time)
            # DVP and KPU overlap; post-processing overlaps next DVP
            bottleneck = 0
            bottleneck_name = ""
            for s in stages:
                if s in EXPECTED_TIMINGS:
                    t = EXPECTED_TIMINGS[s]["typical_us"]
                    if t > bottleneck:
                        bottleneck = t
                        bottleneck_name = s
            fps = 1_000_000 / bottleneck if bottleneck > 0 else 0
            serial_total = sum(EXPECTED_TIMINGS[s]["typical_us"]
                              for s in stages if s in EXPECTED_TIMINGS)
            serial_fps = 1_000_000 / serial_total if serial_total > 0 else 0
        else:
            # Sequential: FPS = 1 / sum(stage_times)
            total = sum(EXPECTED_TIMINGS[s]["typical_us"]
                        for s in stages if s in EXPECTED_TIMINGS)
            fps = 1_000_000 / total if total > 0 else 0
            serial_fps = fps
            bottleneck_name = ""
            bottleneck = 0
            for s in stages:
                if s in EXPECTED_TIMINGS:
                    t = EXPECTED_TIMINGS[s]["typical_us"]
                    if t > bottleneck:
                        bottleneck = t
                        bottleneck_name = s

        pipe_label = "pipelined" if pipelined else "sequential"
        print(f"\n  {mode_name} ({pipe_label}):")
        print(f"    Stages: {' → '.join(stages)}")
        print(f"    Expected FPS: {fps:.1f} (serial: {serial_fps:.1f})")
        print(f"    Bottleneck:   {bottleneck_name} ({bottleneck} µs)")
        if "note" in mode_cfg:
            print(f"    Note: {mode_cfg['note']}")

    print("\n--- Top Optimization Recommendations ---")
    print("  1. HEADLESS MODE: Skip LCD → save 8 ms/frame → +8 FPS")
    print("  2. OVERCLOCK:     400→600 MHz → ~20% faster CPU stages")
    print("  3. DUAL-CORE:     AprilTag/ArUco on core 1, KPU on core 0")
    print("  4. SMALLER INPUT: 224×224 instead of 320×256 → ~25% faster KPU")
    print("  5. MOTION GATE:   Skip KPU when scene is static → 80% power saving")
    print("  6. FRAME SKIP:    Process every 2nd frame → 2× less CPU, same latency")
    print("  7. INT8 QUANT:    Integer-only model → better KPU packing")
    print("  8. PARTIAL LCD:   Only update bbox regions → ~50% LCD savings")
    print()


def run_benchmark(port, baud, duration, mode=None):
    """Run a live benchmark on the connected device."""
    try:
        import serial
    except ImportError:
        print("ERROR: pyserial not installed. Run: pip install pyserial")
        print("Or use --theoretical for offline analysis.")
        return 1

    print(f"Connecting to {port} @ {baud} baud...")
    ser = serial.Serial(port, baud, timeout=1)

    # Set mode if requested
    if mode:
        mode_map = {
            "detect": 0x00,
            "detect_track": 0x20,
            "apriltag": 0x12,
            "aruco": 0x16,
            "landing": 0x22,
            "motion": 0x17,
        }
        mode_id = mode_map.get(mode.lower(), 0x20)
        pkt = build_dlp_packet(DLP_CMD_SET_MODE, bytes([mode_id]))
        ser.write(pkt)
        time.sleep(0.1)
        ser.read(256)  # drain response

    # Reset counters
    ser.write(build_dlp_packet(DLP_CMD_BENCH_RESET))
    time.sleep(0.1)
    ser.read(256)

    # Enable profiling
    ser.write(build_dlp_packet(DLP_CMD_BENCH_ENABLE, b"\x01"))
    time.sleep(0.1)
    ser.read(256)

    print(f"Profiling for {duration} seconds...")
    time.sleep(duration)

    # Request report
    ser.write(build_dlp_packet(DLP_CMD_BENCH_REPORT))
    time.sleep(0.2)
    raw = ser.read(512)

    result = parse_dlp_response(raw)
    if result:
        cmd, payload = result
        report = parse_bench_report(payload)
        if report:
            print_device_report(report)
        else:
            print("Failed to parse benchmark report. Requesting print to UART...")
            ser.write(build_dlp_packet(DLP_CMD_BENCH_PRINT))
    else:
        print("No DLP response. The device may be using debug UART output.")
        print("Requesting print to debug console...")
        ser.write(build_dlp_packet(DLP_CMD_BENCH_PRINT))

    # Disable profiling
    ser.write(build_dlp_packet(DLP_CMD_BENCH_ENABLE, b"\x00"))
    time.sleep(0.1)

    ser.close()
    return 0


def print_device_report(report):
    """Pretty-print a benchmark report from the device."""
    fps = report["fps_x10"] / 10.0
    kpu_util = report["kpu_util_x10"] / 10.0

    print(f"\n{'='*60}")
    print(f"  DRISTY LIVE BENCHMARK — {report['frames']} frames")
    print(f"  FPS: {fps:.1f}  KPU Utilisation: {kpu_util:.1f}%")
    print(f"{'='*60}")
    print(f"  {'Stage':<18s} {'Mean':>7s} {'Max':>7s} {'P95':>7s} {'Count':>7s}")
    print(f"  {'─'*18} {'─'*7} {'─'*7} {'─'*7} {'─'*7}")

    for s in report.get("stages", []):
        if s["count"] == 0:
            continue
        print(f"  {s['name']:<18s} {s['mean_us']:>6d}µ {s['max_us']:>6d}µ "
              f"{s['p95_us']:>6d}µ {s['count']:>7d}")

    # Compare to theoretical
    print(f"\n--- vs Theoretical Ceilings ---")
    for s in report.get("stages", []):
        name = s["name"]
        if name in EXPECTED_TIMINGS and s["count"] > 0:
            expected = EXPECTED_TIMINGS[name]["typical_us"]
            actual = s["mean_us"]
            if expected > 0:
                ratio = actual / expected
                status = "✓" if ratio < 1.5 else "⚠" if ratio < 2.5 else "✗"
                print(f"  {status} {name:<18s}: {actual:>6d}µ vs {expected:>6d}µ expected "
                      f"({ratio:.1f}×)")

    print()


def main():
    parser = argparse.ArgumentParser(description="Dristy Benchmark Runner")
    parser.add_argument("--port", default="/dev/ttyUSB0", help="UART port")
    parser.add_argument("--baud", default=115200, type=int)
    parser.add_argument("--duration", default=10, type=int,
                        help="Profiling duration in seconds")
    parser.add_argument("--mode", help="Vision mode to benchmark")
    parser.add_argument("--theoretical", action="store_true",
                        help="Print theoretical analysis only (no device)")
    args = parser.parse_args()

    if args.theoretical:
        print_theoretical()
        return 0

    return run_benchmark(args.port, args.baud, args.duration, args.mode)


if __name__ == "__main__":
    sys.exit(main() or 0)
