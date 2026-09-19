# nncase / kmodel compilation — deep technical report

## Critical decision: v0.2.0-beta4 → kmodel v4 is the correct default

For standalone-SDK firmware (which is what Dristy is), kmodel v4 via
nncase v0.2.0-beta4 is the highest-confidence path. The runtime is already
vendored in-tree at `lib/nncase/v0` as full C++ source — no downloads, no
version-matching dance.

Use kmodel v3 (nncase 0.1.0-rc5) only when the model fits its narrow operator
set and you want maximum speed and minimum RAM. kmodel v5 (nncase 1.x) is
possible but fragile. nncase v2.x does not support K210 at all.

## Version landscape

| nncase | Produces | Runtime location | Notes |
|---|---|---|---|
| v0.1.0-rc5 | kmodel v3 | `lib/drivers/kpu.c` (native C) | Fastest, fewest ops |
| **v0.2.0-beta4** | **kmodel v4** | **`lib/nncase/v0` (vendored C++ source)** | Best coverage/risk ratio |
| v1.0–v1.9.0 | kmodel v5 | `nncaseruntime-k210.zip` release asset | Must hand-match versions |
| v2.0+ | — | None for K210 | K210 support removed |

### Runtime dispatch in SDK

```c
// lib/nncase/nncase.cpp
if (header->version == 4)
    return nncase_v0_load_kmodel(ctx, buffer);  // v4 → in-tree C++
else
    return nncase_v1_load_kmodel(ctx, buffer);  // v5 → prebuilt static lib
```

`kpu_load_kmodel()` in `lib/drivers/kpu.c` handles v3 natively in C and
delegates everything else to `nncase_load_kmodel`. Use the SDK `develop`
branch, not a release tag — v0.5.6 predates kmodel v4.

## kmodel v3 binary format

Layout: `header | output descriptors | layer headers | layer bodies`

- Header: 28 bytes, first word is literally `3` (no magic number)
- Alignment: layer bodies individually padded to 8-byte boundaries
- Hardware-imposed alignment inside body:
  - Weights: **128 bytes** (`kernel_load_cfg.para_start_addr`)
  - BatchNorm table: **8 bytes** (`bwsx_base_addr`)
  - Activation table: **256 bytes** (`active_addr`)
  - Feature maps: **64 bytes** (`image_addr` in 64-byte units)

### v3 layer type enum (complete operator set)

`KL_K210_CONV(10240)` is the ONLY op that touches the KPU. Everything else is
CPU: `KL_ADD`, `KL_QUANTIZED_ADD`, `KL_GLOBAL_MAX_POOL2D`,
`KL_QUANTIZED_GLOBAL_MAX_POOL2D`, `KL_GLOBAL_AVERAGE_POOL2D`,
`KL_QUANTIZED_GLOBAL_AVERAGE_POOL2D`, `KL_MAX_POOL2D`,
`KL_QUANTIZED_MAX_POOL2D`, `KL_AVERAGE_POOL2D`,
`KL_QUANTIZED_AVERAGE_POOL2D`, `KL_QUANTIZE`, `KL_DEQUANTIZE`,
`KL_REQUANTIZE`, `KL_L2_NORMALIZATION`, `KL_SOFTMAX`, `KL_CONCAT`,
`KL_QUANTIZED_CONCAT`, `KL_FULLY_CONNECTED`,
`KL_QUANTIZED_FULLY_CONNECTED`, `KL_TENSORFLOW_FLATTEN`,
`KL_QUANTIZED_TENSORFLOW_FLATTEN`, `KL_RESIZE_NEAREST_NEIGHBOR`,
`KL_QUANTIZED_RESIZE_NEAREST_NEIGHBOR`, `KL_CHANNELWISE_DEQUANTIZE`,
`KL_LOGISTIC`, plus `KL_K210_ADD_PADDING`, `KL_K210_REMOVE_PADDING`,
`KL_K210_UPLOAD`.

## kmodel v4 opcode table (from `runtime_op.def`)

```
neutral: binary 0x0, concat 0x1, conv2d 0x2, dequantize 0x3, matmul 0x4,
  pad 0x5, quantize 0x6, reduce 0x7, reduce_window2d 0x8, memory_copy 0x9,
  resize_image 0xA, softmax 0xB, transpose 0xC, strided_slice 0xD,
  unary 0xE, quantized_conv2d 0xF, quantized_matmul 0x10,
  quantized_binary 0x11, table_lookup1d 0x12, conv2d_transpose 0x13,
  nnil_unary_method 0x14
k210: kpu_upload 0x2001, kpu_conv2d 0x2002
```

Only `0x2002 kpu_conv2d` touches the KPU.

## Quantization

- **Post-training only.** nncase refuses already-quantized models.
- Asymmetric uint8, per-tensor by default.
- Feed float32 models. No int8 K210 kmodel exists.
- Calibration: 100–500 representative images from deployment distribution.
- `--calibrate-method l2` searches optimal clipping ranges (better than min/max).

### Normalization trap

`--input-mean`/`--input-std` apply to the calibration pass. `--input-type`
decides what deployed firmware feeds the model:
- `--input-type uint8` → feed raw RGB888 uint8 planar. No normalization in C.
  **This is what you want for camera input.**
- Mismatched normalization (training in [-1,1], compiling with default, feeding
  uint8) compiles, runs, and gives garbage accuracy with no warning.

## The zero-copy input path

If model input width is a **multiple of 64** (64, 128, 192, 256, 320), the
input is a single DMA transfer with zero CPU involvement. Width 224 (ImageNet
default) misses this and pays a CPU copy every frame.

From `kpu_upload_core()`:
- width ≤ 16: 4 channels share a 64-byte line
- width ≤ 32: 2 channels share a 64-byte line
- width > 32: rows padded to 64-byte multiples

**This is the single highest-leverage design decision: make input width 64,
128, 192, 256, or 320.**

## Exact ncc command lines

### v0.1.0-rc5 (kmodel v3)

```bash
./ncc -i tflite -o k210model --dataset ./images model.tflite model.kmodel
```

### v0.2.0-beta4 (kmodel v4) — recommended

```bash
./ncc compile model.tflite model.kmodel \
  -i tflite -o kmodel -t k210 \
  --inference-type uint8 \
  --dataset ./calib_images --dataset-format image \
  --input-type uint8 \
  --input-mean 0 --input-std 1 \
  --calibrate-method l2 \
  --dump-ir --dump-weights-range -v
```

`--dump-ir` writes `.dot` graphs. `main.sched` = CPU ops, `k210_0.sched` = KPU
ops. Any layer you expected to be accelerated that appears in `main.sched` is
your bottleneck.

## Conversion pipeline

**Route through TFLite, not ONNX.** v0.2.0 TFLite importer supports ~42 ops;
ONNX importer supports 36 and is missing Sigmoid, Split, Gather,
GlobalAveragePool.

For PyTorch: ONNX→TFLite via onnx2tflite or openvino2tensorflow. Do not feed
ONNX to v0.2.0 directly unless your op set is small.

## Common failure modes

| Symptom | Cause | Fix |
|---|---|---|
| `Fatal: DEQUANTIZE` | Pre-quantized TFLite | Re-export float32 |
| `KPU allocator cannot allocate` | Layer featuremap > 2 MiB | Reduce spatial/channels |
| `kpu_run_kmodel` hangs | v1 runtime ≠ compiler version | Match `nncaseruntime-k210.zip` |
| Model runs, output nonsense | Normalization mismatch | Verify with `ncc infer` |
| Inference slower than expected | Ops fell to CPU | Read `--dump-ir` graph |

## Key correction to our prior assumption

We previously pinned on kmodel v3 (nncase 0.1.0-rc5) as the only choice. The
research shows **kmodel v4 is the better default for standalone firmware**:
- The runtime is already in our SDK tree as buildable C++ source
- It supports significantly more operators (ONNX import, transpose, strided
  slice, conv2d_transpose, dilated conv, etc.)
- The extra v4 operators that ARE NOT KPU-accelerated are clearly identified in
  the opcode table, so we can make informed design tradeoffs
- We can read and patch the runtime source directly

v3 remains optimal for pure-CNN classifiers and tiny-YOLOv2-shaped detectors
where every layer maps to `KL_K210_CONV`. Use v3 for those, v4 for anything
that needs the extra operator coverage.
