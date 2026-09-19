# Object Detection on K210 — Evidence-Based Assessment (2026)

Produced by deep research across ~35 searches. Every number tagged as measured
or claimed. Dead ends documented as carefully as successes.

## Critical context

**Canaan shut down Kendryte on June 23, 2025.** K210/K510/K230 all
discontinued. nncase dropped K210 support in v2.0 (June 2023); the last
K210-capable release is 1.9.0. There is no 2025/2026 toolchain and there will
not be one. We are building on a fully frozen stack.

**Trap:** Kendryte's own support site has 2025-era articles titled "How to
convert YOLO PT models to KModel" with nncase 2.11.0 instructions. **That is
for K230, not K210.** nncase 2.x cannot target K210.

## kmodel version decision

| kmodel | Compiler | Loadable by | Character |
|---|---|---|---|
| **v3** | nncase 0.1.0-RC5 | MaixPy, CanMV, bare C | Fewest ops, genuinely HW-accelerated, smallest RAM, fastest |
| v4 | nncase 0.2.0-beta4 | MaixPy (v4 firmware), bare C | Extra ops are **software with no HW acceleration**, much more RAM |
| v5 | nncase 1.0–1.9 | **bare-metal only** — not MaixPy | Most ops, requires C firmware |

Sipeed's guidance: use V3 whenever possible. V4 only when an operator is
missing. Our pinned nncase 0.1.0-rc5 → kmodel V3 is the correct choice.

## What actually works — measured results

| Architecture | Resolution | FPS | mAP | kmodel | Source |
|---|---|---|---|---|---|
| tiny-YOLOv2, 20-class VOC | 320×256, 10×8 grid | 18 | VOC-20 | v3 | UCM thesis |
| MobileNetV1-α0.25 + YOLOv2, 1 class | 224×224 | 16 / ~30 dual-buff | 0.284 | v3 | UCM thesis |
| **MobileNetV1-α0.50 + YOLOv2, 1 class** | 224×224 | **15 / 29 dual-buff** | **0.390** | v3 | UCM thesis ★ |
| MobileNetV1-α0.75 + YOLOv2 | 224×224 | 12 / 20 dual-buff | 0.364 | v3 | UCM thesis |
| Custom yolo3, hand-optimized runtime | — | **28.6 (35 ms throughput)** | production | custom | Kapernikov/Telraam |

α0.50 beats α0.75 on both mAP and speed — the larger backbone overfits.

## Modern YOLO is structurally not viable

Three independent walls, not a matter of effort:

1. **Toolchain.** v3 supports too few ops; v5 can't load in MaixPy; both paths
   dead-end.
2. **Compute.** 0.25 TOPS. YOLO-Fastest at 0.252 BFLOPs is already ~1 GOP/s at
   4 FPS. YOLOv5s is 68× heavier.
3. **Convergence.** Strip SiLU, SPPF, C2f, the decoupled head, multi-scale FPN
   and the 6×6 stem, constrain to 1×1/3×3 stride-1/2 with 2×2 pooling — you
   have re-derived MobileNet-YOLOv2. Which already exists and runs at 15–29 FPS.

YOLOX-nano measured at **1100–1538 ms per frame** (~0.9 FPS). nncase maintainer:
"~1000 ms is normal. YOLOX-nano has 3 pooling layers the KPU cannot compute.
Suggestion: design a model similar to 20class-yolo."

## Operators that break modern detectors on K210

| Component | Op | K210 | Consequence |
|---|---|---|---|
| Focus | SpaceToDepth | ❌ hard fail | Won't import |
| YOLOv5 v6.0 stem | Conv k=6 s=2 | ❌ not 1x1/3x3 | Also non-KPU |
| SiLU | Logistic × Mul | ⚠️ CPU | Breaks fusion, quantize round-trips |
| SPPF | MaxPool k=5/9/13 s1 | ❌ | Confirmed YOLOX-nano killer |
| C3/C2f Add | binary Add | ⚠️ reference impl | "not optimized" per maintainer |
| FPN upsample | ResizeNN/Bilinear | ⚠️ very slow | Kapernikov's single slowest op |
| Anchor-free head | Split/Pack/Squeeze | ❌ | Must restructure entirely |
| RT-DETR | MatMul | Pad+1×1+Crop | Structurally impossible |

SiLU→ReLU costs −1.9 mAP (measured on YOLOv5s/COCO) but buys nothing on K210
because the remaining ops still fall off the accelerator.

## Speed techniques, ranked by measured payoff

1. **`dual_buff=True`** — one line, ~2× FPS (12→20, 15→29). Costs ~384 KB.
2. **Pipeline across both cores** — 134→35 ms throughput (Kapernikov). Camera
   overlap +41 ms, core 1 inference +10 ms, DMA upload hack +more.
3. **Patch runtime software ops**: integer 2× upsample (1 ms vs slowest op),
   delete concat via memory layout (free), cancel quant/dequant pairs.
4. **Don't let nncase insert normalization** — `--input-mean`/`--input-std`
   emits "extremely slow" software op. Fold into first conv's weights offline.
5. **Inspect `dump_ir=True`**: `main.sched` = CPU, `k210_0.sched` = KPU. Any
   layer in main.sched is your bottleneck.
6. **Explicit ZeroPadding before stride-2 convs** — without it, V3 errors; V4
   uses software computation.
7. **320×256 net on 320×240 camera** — K210 cannot resize cheaply. 256/32=8
   gives integer grid. Never use 224×224 with a QVGA sensor.

## Beyond detection

### Tracking
No learned tracker on K210. IoU+Kalman on CPU is production-proven: Telraam S2
ships it, 99.5% accuracy on total count, 85–95% per-class. **KCF via hardware
FFT is an unexploited gap** — nobody has built it.

### Optical flow
`image.find_displacement()` does **phase correlation via hardware FFT**. Sub-pixel
x/y translation + rotation/scale. Use small power-of-two grayscale ROI (64×32
or 64×64 mean-pooled to 32×32). Reject response < 0.1.

### Segmentation
aXeleRate supports SegNet-basic with MobileNet encoders → kmodel. Expect single-digit FPS. U-Net is a poor fit (transposed convs, skip concats, decoder blows 2 MiB).

### Face recognition
Best-supported pipeline: 3 chained KPU models (face detect → 5-point landmarks
→ MobileFaceNet embedding). Official across multiple vendors with working code.

### Pose estimation
Face landmarks (5-point, 68-point) work. Full-body pose: undemonstrated on K210.

## Recommended path for Dristy

**Week-one baseline:** MobileNetV1-α0.50 + YOLOv2, 320×256 on 320×240 camera,
1–3 classes, k-means anchors (k=5, 1−IoU), aXeleRate legacy-yolov2, ncc
0.1.0-RC5 → kmodel v3, dual_buff. Expect 20–29 FPS, mAP ~0.35–0.45.

**Frontier:** Kapernikov path. Co-design network + patched runtime, pipeline
across cores. 35 ms throughput in a shipping product, with headroom for a
larger network.

## Dead ends — do not attempt

1. nncase ≥ 2.0 for K210 (support removed)
2. kmodel v5 with MaixPy (bare-metal only)
3. YOLOX-nano (1.1 s/frame, confirmed expected)
4. Any transformer detector (structurally impossible)
5. Focus/SpaceToDepth (hard fail)
6. SPPF MaxPool k=5/9/13 (not KPU pooling)
7. MobileNet-SSD native head (Split/Pack/TopK all ❌)
8. Input > 320×240 (hardware ceiling)
9. Dynamic shapes (always onnxsim first)
10. Pre-quantized models (nncase needs float, does its own PTQ)
11. --input-mean/--input-std (inserts very slow software op)
12. PReLU (explicitly unsupported)
13. Body pose estimation (no credible K210 demo exists)
14. Weight-magnitude pruning without recompiling (zeroed weights cost the same)
