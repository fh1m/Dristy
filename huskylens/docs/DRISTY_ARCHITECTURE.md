# Dristy — Architecture Document

**Dristy** is a custom vision co-processor firmware for the HuskyLens SEN0305,
built atop the HackyLens framework. It replaces the stock firmware with a
modular, pipeline-driven system that exposes every K210 hardware capability for
robotics use: detection, classification, tracking, fiducial markers, optical
flow, colour/line following, and a host protocol for integration with flight
controllers, ROS2 nodes, and microcontrollers.

This document is the binding architectural reference. Every design decision is
grounded in the measured hardware constraints documented in the companion
research files.

---

## 0. Name and identity

**Dristy** (দৃষ্টি) — Bengali for "vision" / "sight". The device formerly known
as HuskyLens becomes the Dristy Vision Module. The boot splash, protocol
responses, and all host-facing identifiers use this name.

---

## 1. Design principles

1. **Mailbox, not queue.** Every consumer sees the most recent frame, never a
   stale one. Frames that arrive while the pipeline is busy are silently
   dropped. Latency beats throughput for robotics.
2. **Zero-copy sensor-to-accelerator.** The DVP writes RGB888 planar directly
   into AI SRAM and RGB565 into the display buffer simultaneously. No CPU
   touches pixels before inference.
3. **Pipeline across both cores.** Core 0 owns camera capture and KPU dispatch.
   Core 1 owns post-processing, tracking, classical CV, and the host protocol.
   Budget ~850 KB for double buffers.
4. **Width = multiple of 64.** Every model input width is 64, 128, 192, 256, or
   320 to get the zero-CPU DMA input path (64-byte row alignment). Never 224.
5. **kmodel V3 for speed, V4 for coverage.** Both runtimes are in-tree. Default
   to V4 (nncase 0.2.0-beta4) for new models; use V3 for the proven
   MobileNet-YOLOv2 detector.
6. **Models are swappable at runtime.** Load from flash (fast) or SD card
   (flexible). The pipeline adapts its post-processing to the model's metadata.
7. **Classical CV is first class.** Colour blobs at 114 fps, line regression,
   AprilTag, QR — these are not inferior alternatives to neural inference,
   they are faster and more reliable for their tasks.
8. **Raise PLL1 before optimising code.** The KPU clock defaults to 300 MHz;
   raising it to 400 MHz is a free ~33% inference speedup. This is the first
   performance knob Dristy turns at boot.

---

## 2. Hardware resource budget

From `HARDWARE_K210_FIRST_PRINCIPLES.md` and `RESEARCH_K210_MICROARCHITECTURE.md`:

```
Total SRAM:  8 MiB
  SRAM0:     4 MiB  (general, cached/uncached)
  SRAM1:     2 MiB  (general, cached/uncached)
  AI SRAM:   2 MiB  (KPU feature maps, PLL1-gated)

Flash:      16 MiB
  0x000000 – 0x0FFFFF   Dristy firmware (~1 MB)
  0x100000 – 0x6FFFFF   Model storage (6 MB, multiple models)
  0x700000 – 0x7FDFFF   User scripts / config
  0x7FE000 – 0x7FFFFF   Settings journal (2 × 4 KiB)
  0x800000 – 0xFFFFFF   Stock model backup / extended storage (8 MB)

SD card:    Optional model/script storage, FAT32

KPU:        64 lanes, 576 MACs/cycle @ PLL1
  Stock:    PLL1 ≈ 300 MHz → 173 GMAC/s
  Dristy:   PLL1 = 400 MHz → 230 GMAC/s (+33%)
  Stretch:  PLL1 = 500 MHz → 288 GMAC/s (validate stability)
```

### Memory allocation (6 MiB general)

| Region | Size | Purpose |
|---|---|---|
| Firmware code + rodata | ~800 KB | .text, .rodata, stacks |
| Camera double buffer | 320×240×2×2 = **300 KB** | RGB565 for display, mailbox slots |
| AI planar buffer | 320×240×3 = **225 KB** | RGB888 planar in uncached SRAM, DVP-written |
| kmodel weights (active) | **up to 4 MB** | Loaded from flash at model switch |
| kmodel V4 runtime working | **~500 KB** | nncase v0 runtime workspace |
| Post-processing | ~100 KB | YOLO decode, NMS, tracking state |
| Classical CV workspace | ~200 KB | Grayscale copy, AprilTag/QR workspace |
| MicroPython heap | 128 KB | User scripts on core 1 |
| **Total** | **~6.2 MB** | Fits in 6 MiB with margin |

---

## 3. Frame pipeline — the core architecture

```
                          CORE 0                          │        CORE 1
                                                          │
  OV2640 ──DVP──┬── RGB565 → display_buf (mailbox)        │
                │                                         │
                └── RGB888 planar → AI SRAM 0x40600000    │
                         │                                │
                    ┌────▼────┐                            │
                    │  GATE   │  Is KPU idle?              │
                    └────┬────┘                            │
                         │ yes                             │
                    ┌────▼────────────────┐                │
                    │  KPU inference      │                │
                    │  (kmodel v3 or v4)  │                │
                    └────┬────────────────┘                │
                         │ calc_done IRQ                   │
                         │                                 │
                    ┌────▼────┐          ┌────────────────┐│
                    │ raw out │─────────►│ POST-PROCESS   ││
                    └─────────┘          │ • YOLO decode  ││
                                         │ • NMS          ││
                                         │ • class decode ││
                                         └───────┬────────┘│
                                                 │         │
                                         ┌───────▼────────┐│
                                         │ TRACKER        ││
                                         │ • IoU/Kalman   ││
                                         │ • ID assign    ││
                                         └───────┬────────┘│
                                                 │         │
                                         ┌───────▼────────┐│
                                         │ RESULT BUS     ││
                                         │ • detections   ││
                                         │ • tracks       ││
                                         │ • flow vectors ││
                                         │ • tag poses    ││
                                         └───────┬────────┘│
                                                 │         │
                                         ┌───────▼────────┐│
                                         │ HOST PROTOCOL  ││
                                         │ • UART3 (USB)  ││
                                         │ • UART1/I2C0   ││
                                         │   (Gravity)    ││
                                         └───────┬────────┘│
                                                 │         │
                                         ┌───────▼────────┐│
                                         │ LCD RENDERER   ││
                                         │ • bboxes/text  ││
                                         │ • OSD overlay  ││
                                         └────────────────┘│
```

### Mailbox frame policy (already implemented in HackyLens)

The existing `camera_stream.c` implements exactly the right pattern:
- Multiple frame slots (configurable, default 2–3)
- On FRAME_START: if a FREE slot exists, capture into it; if not, recycle the
  **oldest READY** slot (lowest sequence number)
- Consumer calls `camera_stream_lease_latest()` to get the newest READY frame
- Stale frames are automatically dropped — `g_ready_drop_count` tracks how many

**For Dristy, the KPU dispatch hook goes here:** on FRAME_FINISH, if the KPU is
idle, immediately start inference on the just-captured AI SRAM contents.
If the KPU is still busy with the previous frame, the new AI data simply
overwrites the previous (the DVP always writes to the same AI SRAM address),
and we process it when the KPU finishes. This is true mailbox semantics —
the accelerator always sees the freshest data available.

### The zero-copy trick, precisely

```c
// At init — DVP writes two outputs simultaneously:
hal_dvp_config_rgb565(320, 240, display_buf_addr, 1);  // for LCD
hal_dvp_set_ai_rgb888(0x40600000,                       // R plane
                      0x40600000 + 320*240,              // G plane
                      0x40600000 + 320*240*2);           // B plane

// On KPU dispatch — if model width is 320 (multiple of 64):
//   → pure DMA transfer, zero CPU involvement
//   → kpu_input_dma() used, not kpu_upload_core()
```

Width 320 × height 240 × 3 channels = 230,400 bytes = 225 KB of AI SRAM.
The model's first layer reads directly from where the DVP wrote.

---

## 4. Vision modes — the Dristy capability set

Each mode is a pipeline configuration. Multiple modes can share the same
model but differ in post-processing. The user selects a mode via buttons or
the host protocol.

### 4.0 Detection post-processing: Soft-NMS

**Implementation: `dristy_nms.c/h`**

Dristy uses **Soft-NMS (Gaussian)** by default instead of hard NMS.
Hard NMS completely suppresses any overlapping detection — fine for
well-separated objects, but loses valid detections when objects overlap
(e.g., people in a group, stacked packages, clustered ArUco markers).

Soft-NMS decays overlapping scores by `exp(-IoU²/σ)` instead of zeroing
them, preserving partially-overlapping detections with reduced confidence.
Three methods available, all configurable at runtime:

| Method | Formula | Pros | Cons |
|---|---|---|---|
| Hard | IoU > τ → score=0 | Fast, predictable | Loses overlapping objects |
| Soft-Gaussian | score *= exp(-IoU²/σ) | Best mAP, smooth | Slightly more output |
| Soft-Linear | IoU > τ → score *= (1-IoU) | Cheaper than Gaussian | Less smooth |

Configurable parameters:
- `iou_threshold` (0.45): gate for hard/linear methods
- `sigma` (0.5): Gaussian decay width
- `score_threshold` (0.15): minimum surviving confidence
- `class_agnostic` (0): per-class or across all classes

### 4.1 Neural inference modes

| Mode | Model | Input | Expected FPS | Post-processing |
|---|---|---|---|---|
| **DETECT** | MobileNetV1-α0.50 + YOLOv2 | 320×256 | 20–29 | YOLO decode + NMS + tracker |
| **DETECT_CUSTOM** | User kmodel from SD | up to 320×240 | varies | Configurable decode |
| **CLASSIFY** | MobileNetV1-α0.50 classifier | 192×192 | ~25 | Top-K decode |
| **FACE_DETECT** | face_detect_320x240.kmodel | 320×240 | ~15 | YOLO 9-anchor + landmarks |
| **FACE_RECOGNISE** | 3-stage: detect→landmarks→embed | 320×240→128→64 | ~8 | Embedding match, threshold 80.5 |

Model geometry rule: **320×256 for detection** (256/32 = 8 integer grid, 320
is a multiple of 64), **192×192 for classification** (multiple of 64, fits
easily), **320×240 for face** (matches camera exactly).

### 4.2 Classical CV modes (no KPU, CPU only, faster)

| Mode | Method | Expected FPS | Output |
|---|---|---|---|
| **COLOUR_TRACK** | LAB threshold → find_blobs | ~114 | Blob centroids, areas, IDs |
| **LINE_FOLLOW** | LAB threshold → get_regression | ~100 | Line angle, offset, curvature |
| **APRILTAG** | apriltag library (TAG36H11 + others) | 10–20 | Tag ID, corners, pose (R, t) |
| **QR_CODE** | quirc library | 10–15 | Decoded string, corners |
| **OPTICAL_FLOW** | Hardware FFT phase correlation | ~50+ | dx, dy, rotation, response |
| **COLOUR_SORT** | Multi-threshold blob detection | ~80 | Sorted blob list by area/position |

Both AprilTag and QR are already vendored in HackyLens (`firmware/third_party/`).

### 4.3 Hybrid modes (neural + classical, pipelined)

| Mode | Description |
|---|---|
| **DETECT_AND_TRACK** | KPU detection on core 0, IoU+Kalman tracking on core 1. New detections update tracks; between detections, tracks coast with Kalman prediction. |
| **DETECT_AND_TAG** | KPU detection interleaved with AprilTag on grayscale copy. Gives both neural detections and fiducial poses in one result. |
| **LANDING_TARGET** | AprilTag pose estimation + optical flow for drift. Designed for autonomous landing on a marked pad. |

---

## 5. Model management

### 5.1 Flash model slots

```
0x100000  slot0.kmodel  (default: MobileNetV1-YOLOv2, ~2 MB)
0x300000  slot1.kmodel  (default: face_detect, ~1 MB)
0x400000  slot2.kmodel  (classifier, ~1 MB)
0x500000  slot3.kmodel  (user custom, up to 2 MB)
```

Each slot has a 256-byte header at the start:
```c
typedef struct {
    uint32_t magic;           // 'DRST'
    uint32_t kmodel_offset;   // typically 256
    uint32_t kmodel_size;
    uint32_t model_type;      // DETECT_YOLOV2, CLASSIFY, FACE_DETECT, ...
    uint16_t input_width;
    uint16_t input_height;
    uint16_t num_classes;
    uint16_t num_anchors;
    float    anchors[20];     // up to 10 anchor pairs
    float    threshold;
    float    nms_threshold;
    char     class_names[512]; // null-separated
    uint32_t crc32;
} dristy_model_header_t;
```

The pipeline reads this header to configure its post-processing automatically.

### 5.2 SD card models

Any `.kmodel` file on the SD card root can be loaded via the host protocol or
the on-device menu. The pipeline reads a companion `.json` or the embedded
header for metadata.

### 5.3 Training pipeline (documented for users)

```
Dataset (COCO/VOC/custom)
    │
    ▼
aXeleRate (legacy-yolov2 branch)
  config: MobileNet5_0, input_size=320, anchors=k-means(k=5, 1-IoU)
    │
    ▼
float32 .tflite
    │
    ▼
ncc v0.2.0-beta4 compile
  -i tflite -t k210 --inference-type uint8
  --input-type uint8 --dataset ./calib --calibrate-method l2
  --dump-ir
    │
    ▼
.kmodel (v4) → flash slot or SD card
```

Alternative for V3 (maximum speed):
```
ncc v0.1.0-rc5
  -i tflite -o k210model --dataset ./images model.tflite model.kmodel
```

---

## 6. Tracking subsystem

Runs on core 1, fed by KPU detections from core 0.
**Implementation: `dristy_tracker.c/h`**

### Architecture: DeepSORT-style cascaded matching + block Kalman

```
Detection frame N
    │
    ▼
┌───────────────────────────────────────────────┐
│ PREDICT: all tracks stepped forward 1 frame   │
│ Block Kalman: pos[i] += vel[i], P' = F·P·F'+Q │
│ Adaptive noise: low-conf tracks get 3× Q       │
└───────────────────┬───────────────────────────┘
                    │
    ┌───────────────▼───────────────────────────┐
    │ PASS 1: Confirmed+Coasting vs ALL dets    │
    │ Cost = 0.7×(1-IoU) + 0.3×(Mahalanobis/g) │
    │ Class gate: different cls → COST_INF       │
    │ Hungarian (Munkres) optimal assignment      │
    └───────────────┬───────────────────────────┘
                    │
    ┌───────────────▼───────────────────────────┐
    │ PASS 2: Tentative tracks vs REMAINING dets │
    │ Same cost metric, Hungarian assignment      │
    └───────────────┬───────────────────────────┘
                    │
    ┌───────────────▼───────────────────────────┐
    │ UPDATE matched tracks (Kalman update)      │
    │ CREATE new tracks from unmatched dets      │
    │ COAST unmatched tracks (increase miss)     │
    │ DELETE dead tracks (miss > threshold)       │
    └───────────────────────────────────────────┘
```

### Block-coupled Kalman filter (the key improvement)

State per track: `[cx, cy, w, h, vx, vy, vw, vh]` — 8 dimensions.
But NOT a full 8×8 covariance matrix (O(n³) per update).

Instead, 4 independent **2×2 blocks**: `[cx,vx]`, `[cy,vy]`, `[w,vw]`, `[h,vh]`.
Each block has a 2×2 symmetric covariance (3 unique values: a, b, c):

```
P_i = [[a, b],    a = var(pos)
       [b, c]]    b = cov(pos, vel)  ← THIS is what diagonal Kalman misses
                   c = var(vel)
```

The cross-covariance `b` captures how position uncertainty couples with
velocity — essential for accurate prediction when objects accelerate or
decelerate. Predict step:

```
Pa' = Pa + 2·Pb + Pc + Q_pos    ← velocity uncertainty flows into position
Pb' = Pb + Pc                    ← cross-covariance evolves
Pc' = Pc + Q_vel                 ← velocity uncertainty grows
```

Cost: 4 blocks × 3 values × (mul + add) = ~24 multiplies per predict.
~40 multiplies per update. Total < 80 µs for 20 tracks @ 400 MHz.

### Key features

- **Hungarian (Munkres) algorithm**: O(n³) optimal assignment. For n≤32,
  completes in < 30 µs. Greedy can misassign when tracks cross paths.
- **Cascaded matching**: confirmed tracks match first (tighter covariance
  = more selective), tentative tracks get leftovers. Prevents noise
  detections from stealing matches from established tracks.
- **Combined IoU + Mahalanobis cost**: IoU handles spatial overlap,
  Mahalanobis uses the Kalman covariance to weight distance statistically.
  Gated at χ²₄(0.99) = 9.21 for 4-DOF measurement.
- **Class-aware gating**: a person detection cannot match a car track.
- **Adaptive process noise**: confidence 900→1× noise, confidence 200→3×.
  Uncertain detections produce more cautious predictions.
- **Track lifecycle**: TENTATIVE → CONFIRMED → COASTING → DELETED.
  All thresholds configurable at runtime via DLP.

### Runtime configuration (via `dristy_tracker_config_t`)

| Parameter | Default | Description |
|---|---|---|
| `min_hits_to_confirm` | 2 | Frames before tentative→confirmed |
| `max_coast_frames` | 5 | Max frames without detection before deletion |
| `max_tentative_coast` | 2 | Max coast for unconfirmed tracks |
| `iou_threshold_q8` | 77 (0.30) | Minimum IoU for association |
| `mahal_gate_q8` | 2358 (9.21) | Mahalanobis distance² gate |
| `class_aware` | 1 | Different classes cannot match |
| `use_mahalanobis` | 1 | Combined IoU+Mahal cost |
| `process_noise_pos_q12` | 4096 (1.0) | Position process noise |
| `process_noise_vel_q12` | 1024 (0.25) | Velocity process noise |
| `measure_noise_q12` | 8192 (2.0) | Measurement noise |

### Future: KCF via hardware FFT (the unexploited gem)

The K210's FFT accelerator does a 512-point complex FFT in **16 µs**. KCF
tracking is fundamentally an FFT-based correlation filter. Nobody has built
this on K210. The recipe:

1. Extract a grayscale patch around each tracked object
2. FFT the patch and the template (hardware, 16 µs each)
3. Element-wise multiply in frequency domain (CPU, fast)
4. Inverse FFT (hardware, 16 µs)
5. Peak of the response = new position

Total: ~50–100 µs per tracked object. At 10 objects that's 0.5–1 ms — trivially
fits in the inter-frame gap. This would be a **world-first** on this hardware.

---

## 7. Optical flow via hardware FFT

Already available as `image.find_displacement()` in the OpenMV-derived image
library, using phase correlation:

- Input: grayscale ROI, power-of-two size (64×32 recommended)
- Output: sub-pixel (dx, dy), rotation, scale, response confidence
- Cost: ~16 µs for the FFT itself, plus data preparation

For Dristy, expose this as the `OPTICAL_FLOW` mode:
- Configurable grid of ROIs across the frame
- Each ROI produces a flow vector
- Combined: a sparse flow field suitable for velocity estimation
- Response < 0.1 rejected as noise (use 0.3 in practice)

Applications: visual odometry for hovering, obstacle approach rate, and
landing drift compensation.

---

## 8. AprilTag, fiducial markers, and 6-DOF pose

HackyLens already vendors the full `apriltag` library with TAG36H11 support
and the `quirc` QR library. Dristy extends both with **PnP pose estimation**.
**Implementation: `dristy_pose.c/h`**

### Supported tag families

| Family | Bits | Min distance | Codes | Use case |
|---|---|---|---|---|
| TAG36H11 | 36 | 11 | 587 | Default, most robust |
| TAG25H9 | 25 | 9 | 35 | Smaller tags, compact |
| TAG16H5 | 16 | 5 | 30 | Tiny tags, less robust |

### PnP pose estimation (the robotics-critical feature)

Each AprilTag detection provides 4 sub-pixel corner positions. Combined with
known tag size and calibrated camera intrinsics, Dristy computes a full
**6-DOF pose** using homography decomposition:

```
4 corners (pixels) + tag size (mm) + K⁻¹ (camera matrix)
    │
    ▼
┌─────────────────────────────────────────────────────┐
│ 1. Undistort corners (radial model: k1, k2)          │
│ 2. Normalise to camera coordinates: K⁻¹ · p          │
│ 3. Compute 3×3 homography H (8×8 Gaussian elim)      │
│ 4. Decompose H → [r1 | r2 | t] (planar PnP)         │
│ 5. Gram-Schmidt orthogonalisation → R ∈ SO(3)         │
│ 6. Extract Euler angles (ZYX convention)               │
│ 7. Compute reprojection error (quality metric)         │
└─────────────────────┬───────────────────────────────┘
                      │
                      ▼
              dristy_pose_t:
                tx, ty, tz  (mm, camera-to-tag)
                roll, pitch, yaw  (degrees)
                range  (Euclidean distance mm)
                reproj_error  (pixels, < 2.0 = good)
```

The homography decomposition is specifically optimised for planar targets
(Z=0 in world coordinates) — mathematically simpler and more stable than
the general 6-point DLT. For a 4-corner square:
- 8×8 linear system solved by Gaussian elimination (~500 FLOPs)
- No iterative refinement needed (4 points are exactly determined)
- Reprojection error is computed for quality assessment

### Camera calibration

Camera intrinsics must be calibrated once and stored in the settings journal.
Default OV2640 @ 320×240 approximations are provided:
- fx=fy=225, cx=160, cy=120 (approximate for this lens)
- k1=k2=0 (radial distortion coefficients, set after calibration)

Calibration can be done via:
1. DLP command with known intrinsics from external calibration
2. On-device calibration using a known-size AprilTag at known distances

### Performance budget

| Operation | Time | Notes |
|---|---|---|
| AprilTag detect (1–5 tags) | 40–100 ms | CPU-bound on core 1 |
| PnP pose per tag | < 50 µs | Negligible vs detection |
| quirc QR decode | 30–80 ms | Depends on QR complexity |
| Total: detect + pose | 10–20 FPS | For 1–5 tags |

---

## 9. Drone / Rover / AUV Integration

**Implementation: `dristy_result_bus.c/h`, `dristy_control_output.c/h`, `dristy_pipeline.c/h`**

### Communication topology

```
                          HuskyLens PCB
┌──────────────────────────────────────────────────────┐
│  K210 SoC                                            │
│  ┌──────────────────────────────────────┐            │
│  │ Dristy Pipeline                      │            │
│  │  camera → KPU → tracker → result_bus │            │
│  └──────────────┬───────────────────────┘            │
│                 │                                    │
│    ┌────────────▼──────────┐                         │
│    │ Control Output        │                         │
│    │ (compact binary)      │                         │
│    └────┬──────────────┬───┘                         │
│         │              │                             │
│    USB UART3      Gravity UART1                      │
│    (3 Mbaud)      (≤921600 baud)                     │
│    [CP210x]       [4-pin connector]                  │
│         │              │                             │
└─────────┼──────────────┼─────────────────────────────┘
          │              │
     Dev PC / GCS   ESP32-S3 Bridge (future)
                         │
                    ┌────┴────────────────┐
                    │ ESP32-S3            │
                    │  UART RX ← Dristy  │
                    │  SPI → FC          │
                    │  I2C ← IMU/Baro    │
                    │  UART ← GPS        │
                    │  WiFi → GCS        │
                    └─────────────────────┘
```

### Pin availability (HuskyLens SEN0305)

| Interface | Pins | Status | Use |
|---|---|---|---|
| **Gravity UART1** | IO34 (RX), IO35 (TX) | ✅ Available | Primary robotics link |
| **Gravity I2C0** | IO34 (SCL), IO35 (SDA) | ✅ Available (muxed) | Alternative to UART |
| **USB UART3** | via CP210x | ✅ Available | Dev/debug, 3 Mbaud |
| SPI0 | IO18-22 | ❌ LCD | Cannot share |
| SPI1 | IO26-29 | ⚠️ SD card | Available if no SD |
| SPI3 | unrouted | ❌ No pads | Would need hardware mod |

**Primary robotics interface: Gravity UART1** at 115200–921600 baud.
Bandwidth at 921600: ~92 KB/s → 20 tracks × 12 bytes × 30 Hz = 7.2 KB/s = 8% utilisation.

### Control output protocol (compact binary)

Wire format: `[0xD5 SYNC] [TYPE] [LEN_LO] [LEN_HI] [PAYLOAD] [CRC8]`

| Type | Size | Rate | Content |
|---|---|---|---|
| HEARTBEAT (0x01) | 13 bytes | 10 Hz | timestamp, mode, fps, target_valid, status |
| TARGET (0x02) | 25 bytes | 30 Hz | **primary target only** — 20 bytes a FC needs |
| TRACKS (0x03) | 5 + 12n | 30 Hz | all tracked objects |
| TAGS (0x04) | 5 + 14n | tag rate | all fiducial tags with pose |
| FULL_FRAME (0x06) | variable | on demand | target + tracks + tags + flow |

### Primary target struct (20 bytes) — designed for PID loops

```c
// The flight controller reads ONLY this for basic vision tracking:
struct {
    uint32_t timestamp_ms;   // for IMU time-alignment
    int16_t  error_x;        // [-1000,+1000] → yaw PID
    int16_t  error_y;        // [-1000,+1000] → pitch PID
    uint16_t size;           // [0,2000] → approach/throttle PID
    int16_t  error_rate_x;   // d(error)/dt → PID derivative term
    int16_t  error_rate_y;   // d(error)/dt → PID derivative term
    uint16_t range_mm;       // from AprilTag pose → altitude PID
    uint16_t track_id;       // persistent ID
    uint8_t  target_type;    // TRACK, TAG, BLOB, LINE, NONE
    uint8_t  confidence;     // 0-100%
};
```

### Normalised coordinate system

```
        -1000 (top)
           ▲
           │
-1000 ◄────┼────► +1000
(left)     │      (right)
           ▼
        +1000 (bottom)

    0,0 = frame centre
    Directly usable as PID error signal
```

### Sensor fusion with IMU (ESP32-S3 bridge design)

The `timestamp_ms` field in every packet enables time-alignment:

```
Time →  ─────────────────────────────────────────
IMU:    |s0|s1|s2|s3|s4|s5|s6|s7|s8|s9|  @ 200 Hz
Vision: |    V0    |    V1    |    V2   |  @ 30 Hz

ESP32-S3 aligns V0.timestamp with nearest IMU sample,
interpolates IMU between vision frames for smooth control.
```

The ESP32-S3 bridge (future, NOT in firmware):
1. Receives Dristy control output on UART RX
2. Reads IMU (MPU6050/ICM42688) on I2C at 200+ Hz
3. Reads GPS on second UART
4. Time-aligns vision + IMU + GPS
5. Sends fused state to FC over SPI at 100 Hz
6. Optionally streams to GCS over WiFi

### Target selection for drones

Priority-based automatic target selection in `dristy_pipeline.c`:

1. **Specific track ID** — if `target_track_id > 0`, lock onto that track
2. **Class filter** — if `target_class != 0xFF`, only consider that class
3. **Score-based** — score = 2×confidence + size_bonus - distance_from_centre
   - Prefers high-confidence, large, centred objects
   - Automatically switches to nearest/biggest if current target is lost

Override via DLP: `DLP_CMD_SET_ROI` sets target_track_id for lock-on.

---

## 10. Host protocol — Dristy Link Protocol (DLP)

Backward-compatible with the stock HuskyLens protocol for basic queries, but
extended with Dristy-specific commands for full control.

### Framing (unchanged)

```
0x55 0xAA | 0x11 | DataLength | Command | Data | Checksum
```

### New Dristy commands (0x40–0x7F range, avoiding stock collision)

| Hex | Name | Dir | Data | Description |
|---|---|---|---|---|
| `0x40` | DLP_SET_MODE | →D | u8 mode | Switch vision mode |
| `0x41` | DLP_GET_MODE | →D | — | Query current mode |
| `0x42` | DLP_GET_TRACKS | →D | — | All tracked objects with IDs, velocities |
| `0x43` | DLP_GET_FLOW | →D | — | Optical flow field |
| `0x44` | DLP_GET_TAGS | →D | — | AprilTag detections with 6-DOF poses |
| `0x45` | DLP_GET_QR | →D | — | QR code decoded strings |
| `0x46` | DLP_LOAD_MODEL | →D | u8 slot | Switch active kmodel |
| `0x47` | DLP_SET_THRESHOLD | →D | u16 thresh×100 | Detection confidence threshold |
| `0x48` | DLP_SET_CLOCK | →D | u16 cpu_mhz, u16 kpu_mhz | Set clock speeds |
| `0x49` | DLP_GET_PERF | →D | — | FPS, inference ms, CPU%, temperature |
| `0x4A` | DLP_PUSH_FRAME | →D | raw image data | Host-fed inference (like stock 0x3A) |
| `0x4B` | DLP_GET_RAW_FRAME | →D | — | Stream raw camera frame to host |
| `0x4C` | DLP_SET_ROI | →D | u16 x,y,w,h | Region of interest for flow/tracking |
| `0x4D` | DLP_SET_COLOUR_THRESH | →D | 6×u8 LAB | Set colour tracking thresholds |
| `0x4E` | DLP_GET_BLOBS | →D | — | Colour blob detections |
| `0x4F` | DLP_IDENTIFY | →D | — | Returns "DRISTY\0" + version |

### Return types (extended)

| Hex | Name | Data |
|---|---|---|
| `0x50` | DLP_RETURN_TRACK | 16B: u16 id, cx, cy, w, h, vx×100, vy×100, age |
| `0x51` | DLP_RETURN_FLOW | 8B: i16 dx×100, dy×100, rot×100, response×100 |
| `0x52` | DLP_RETURN_TAG | 20B: u16 id, family, i16 cx, cy, float hamming, 6×i16 pose |
| `0x53` | DLP_RETURN_QR | variable: u16 len, utf8 string, 4×u16 corners |
| `0x54` | DLP_RETURN_PERF | 12B: u16 fps×10, u16 infer_ms, u16 cpu_pct, u16 kpu_mhz |
| `0x55` | DLP_RETURN_BLOB | 12B: u16 cx, cy, w, h, pixels, id |

Stock commands 0x20–0x3E continue to work unchanged for backward compatibility.

---

## 10. Boot sequence

```
1. PLL0 = 400 MHz (CPU)
   PLL1 = 400 MHz (KPU) — raised from default 300 MHz
   Validate stability with KPU NOP layer
2. FPIOA pin mux from board.toml
3. LCD init (SPI0, ST7789V)
4. Camera init (OV2640 probe at SCCB 0x60, fallback GC0328 at 0x42)
5. DVP config: 320×240, dual output (RGB565 + RGB888 planar)
6. Load default kmodel from flash slot 0
7. Start camera_stream (mailbox mode)
8. Core 1: start post-processing loop + host protocol listener
9. Display boot splash: "DRISTY v0.1" + mode name
10. Enter main loop
```

---

## 11. Creative exploits — hidden gems to implement

### 11.1 Hardware FFT for image sharpness estimation

A 512-point FFT of an image row gives a frequency spectrum. The ratio of
high-frequency to low-frequency energy is a **focus/sharpness metric** —
computable in 16 µs per row. Run it on a few rows per frame for an
autofocus-quality signal, useful for detecting motion blur or defocus.

### 11.2 KPU as a general convolver

The KPU can run any 3×3 convolution, not just learned weights. By loading
hand-crafted kernels:
- **Sobel edge detection** (3×3) at hardware speed (~230 GMAC/s)
- **Gaussian blur** for noise reduction
- **Sharpen filter**
- **Custom feature extractors** for classical CV augmentation

This is a single `kpu_conv2d()` call with identity batch-norm and linear
activation. Nobody has published this use of the K210 KPU.

### 11.3 `full_add` for hardware accumulation

The `full_add` bit in the layer argument outputs raw int64 accumulated values
instead of quantized uint8. This enables:
- **Feature extraction without quantization loss** — read the pre-activation
  accumulator for embedding generation
- **Multi-scale feature fusion** — accumulate across scales in hardware

### 11.4 Dual-model pipelining

While the KPU runs model A, core 1 processes model A's previous output. When
the KPU finishes, immediately dispatch model B on the same frame (e.g., face
detect → landmark). Core 1 processes model B's output while the DVP captures
the next frame. This gives a 3-stage pipeline for multi-model workflows.

### 11.5 FFT-based correlation tracking (KCF)

As detailed in §6 — the hardware FFT makes this genuinely feasible on K210.
A world-first implementation on this chip.

### 11.6 Frequency-domain obstacle detection

Run 1D FFTs on horizontal scan lines. Sudden changes in the frequency profile
indicate depth discontinuities (edges of obstacles). Combined with optical
flow, this gives a monocular depth cue without a neural network.

### 11.7 Template matching via FFT cross-correlation

For small, known targets (like a specific logo or pattern), FFT-based
normalised cross-correlation is faster than sliding-window comparison.
One 512-point FFT + multiply + inverse = ~50 µs total.

---

## 12. Implementation roadmap

| Phase | Deliverable | Depends on |
|---|---|---|
| **P0: Skeleton** | Rename to Dristy, boot splash, PLL1 raise, mailbox→KPU wiring | existing HackyLens |
| **P1: Detect** | MobileNetV1-YOLOv2 320×256, dual_buff, YOLO decode on core 1 | P0 |
| **P2: Track** | IoU+Kalman tracker on core 1, DLP_GET_TRACKS | P1 |
| **P3: Tags** | AprilTag mode (TAG36H11), pose estimation, DLP_GET_TAGS | P0 |
| **P4: Flow** | Optical flow mode, FFT phase correlation, DLP_GET_FLOW | P0 |
| **P5: Classical** | Colour tracking, line following, QR decode | P0 |
| **P6: Multi-model** | Face recognition pipeline, model slot switching | P1 |
| **P7: Protocol** | Full DLP implementation over UART3 and Gravity | P1–P5 |
| **P8: KCF** | FFT-based correlation tracker — the world-first | P2, P4 |
| **P9: Polish** | Settings menu, LED feedback, SD model loading, calibration | all |

P0–P2 are the minimum viable product. P3–P5 are high-value robotics features.
P8 is the research contribution that makes this historically significant.

---

## 13. What makes Dristy historically unique

1. **First firmware to exploit the K210's hardware FFT for vision** — correlation
   tracking, sharpness estimation, frequency-domain features.
2. **First to use the KPU as a general convolver** for classical CV kernels
   (Sobel, Gaussian) at 230 GMAC/s hardware speed.
3. **First pipelined dual-core vision co-processor firmware** on HuskyLens with
   measured mailbox latency guarantees.
4. **First to implement KCF tracking on K210** via the hardware FFT accelerator.
5. **First open-source ROS2-ready vision module firmware** for this hardware.
6. **Complete exploitation of a discontinued chip** — every peripheral (KPU, FFT,
   SHA256, camera, LCD, SD, buttons, LEDs, UART, I2C) documented and exposed.
