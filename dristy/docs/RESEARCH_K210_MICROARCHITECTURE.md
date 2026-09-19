# K210 SoC Microarchitecture — Engineering Reference

Sourced primarily from the Chinese K210 技术参考手册 (Technical Reference Manual,
472 pages), the Kendryte Standalone SDK source, and the nncase compiler source.
The Chinese TRM contains an RTL-level block decomposition of the KPU that does
NOT appear in the English datasheet.

## CPU

| Property | Value |
|---|---|
| Cores | 2 × RV64GC (RV64IMAFDC), symmetric |
| Microarchitecture | Rocket-derived, single-issue, in-order, 5-stage |
| Branch prediction | BTB + BHT + RAS, local and global history |
| L1 I-cache | 32 KiB, 8-way, 64 B line, per core |
| L1 D-cache | 32 KiB, 8-way, 64 B line, per core |
| L2 | **None** |
| FPU | Per-core, IEEE 754-2008, single AND double precision, hardware FMA/div/sqrt |
| MMU | Sv39 but unusable — implements non-ratified priv spec v1.9.1 |
| Custom ISA | **None.** KPU is AXI-attached peripheral, not RoCC coprocessor |

**Inter-core cache coherence: UNVERIFIED — assume none.** The TRM's coherence
claim is inside a paragraph known to be erroneous boilerplate (copy-pasted from
a SiFive multi-core product, claiming 4 cores when K210 has 2). All K210
codebases sidestep the question by using uncached `0x4xxxxxxx` aliases for
shared buffers.

## Memory — correcting widely-repeated errors

### SRAM is asymmetric (not 3+3)

| Bank | Size | Cached alias | Uncached alias |
|---|---|---|---|
| MEM0 (SRAM0) | **4 MiB** | 0x80000000 | 0x40000000 |
| MEM1 (SRAM1) | **2 MiB** | 0x80400000 | 0x40400000 |
| AI SRAM | **2 MiB** | 0x80600000 | 0x40600000 |

The common "6 MiB general = 3+3" claim is wrong. It is 4+2.

### DMA controller: 6 channels, not 8

`DMAC_CHANNEL_MAX` enumerates channels 0–5. The "8 channels" figure circulates
widely and is wrong. It is a Synopsys DesignWare axi_dmac.

### Cached vs uncached aliases

- `0x8xxxxxxx` → through CPU L1 D-cache. Lower latency for CPU.
- `0x4xxxxxxx` → direct AXI, bypassing cache. Required for DMA/DVP/KPU buffers.

**FP loads reportedly do not work from the 0x4xxxxxxx region** (laanwj,
single source, unverified).

## KPU internal block structure

```
AXI bus
 ├─ gs ──► cfg FIFO (13 layers deep) ──► regfile ──► main_ctrl FSM
 ├─ gm ──► weight/BN/activation parameter fetch
 └────────► ram_mux ──┬──► main_mem (32K × 66 bytes = 2 MiB feature maps)
                      └──► para_ram (2 × 4096×144b weight + 1056×60b BN)
                           │
                           ▼
                      alu_cluster
                       ├─ ai_alu_glue  (64 × 3-byte sliding window fan-out)
                       ├─ ai_alu       (64 parallel MAC units)
                       ├─ ai_alu_acc   (input-channel accumulator)
                       ├─ ai_act       (16-segment PWL activation LUT)
                       ├─ ai_pool      (10 pooling modes)
                       └─ ai_alu_wb    (write-back)
```

## The 66-byte RAM — why only 1×1 and 3×3 work

The feature-map RAM (`main_mem`) is physically **66 bytes wide**, not 64:
- Indices 1–64 hold real pixel data
- Index 0 is a hardware-replicated copy of byte 64 of the PREVIOUS row
- Index 65 is a copy of byte 1 of the NEXT row

This ±1 halo enables 64 overlapping 3-wide sliding windows from a single
64-byte row read per cycle, with zero stall. A 5×5 kernel would need a ±2
halo (68-byte RAM), 7×7 would need ±3. **The halo width is hardwired at ±1.**
This is the fundamental hardware reason only 1×1 and 3×3 kernels exist.

## The MAC array — 64 lanes × 9 taps = 576 MACs/cycle

From the TRM's `ai_alu_glue` description: each clock reads 64 bytes, fans them
out as 64 overlapping 3-byte windows into 64 parallel ALUs.

| Clock | MACs/s | "TOPS" |
|---|---|---|
| 400 MHz | 230.4 G | 0.23 |
| 600 MHz | 345.6 G | 0.35 |
| 800 MHz | 460.8 G | 0.46 |

**This is output-stationary SIMD, NOT a systolic array** despite what several
secondary sources claim. The loop nest is output-channel outermost,
input-channel inner, with partial sums in an accumulator plane.

### The 1×1 convolution cliff

1×1 convolutions use only **1 of 9 taps** per lane, so peak drops to
64 MACs/cycle = 25.6 GMAC/s — a **9× throughput cliff**. Since MobileNet
spends most of its work in 1×1 pointwise layers, this is the main reason
MobileNet on K210 lands far below peak TOPS.

## Feature-map layout and lane utilization

Width determines how many of the 64 ALU lanes are used:

| Width | Groups | Lane utilization |
|---|---|---|
| 16 | 4 (pack 4 channels/row) | **100%** |
| 32 | 2 (pack 2 channels/row) | **100%** |
| 64, 128, 192, 256, 320 | 1 | **100%** |
| 20 | 2 | 62.5% |
| 33 | 1 | 51.6% |
| **65** | 1 | **50.8%** ← worst case |
| 224 | 1 | 87.5% |

**Width 65 wastes nearly half the array.** Design widths to be 16, 32, or
a multiple of 64. This is a potential 2× swing invisible in FLOP counts.

## Weight tiling constraint

Per-load budget is 30 KiB (nncase conservative; TRM variously says 36 or 72).
Max 64 loads per layer (6-bit `load_time` field).

**Max Cout for 3×3 conv:**

| Cin | Channels per load | Max Cout |
|---|---|---|
| 64 | 53 | 1024 (not binding) |
| 128 | 26 | 1024 (not binding) |
| 256 | 13 | 832 |
| 512 | 6 | 384 |
| 1024 | 3 | 192 |

For 1×1, divide `one_channel_size` by 9 — limit essentially never binds.

## Numeric format

uint8 in, uint8 out. Internal accumulator is int64 (reference model).

The hardware computes the full zero-point-corrected product:
```
Σ(x−zx)(w−zw) = Σxw − zw·Σx − zx·Σw + K²·Cin·zx·zw
```
via `arg_x = −zw`, `arg_w = −zx`, `arg_add = K²·zw·zx`.

**pad_value must be set to the input zero-point**, not literal zero. Padding
with numeric 0 in a quantized uint8 tensor injects a large negative value.

Activation: 16-segment piecewise-linear fit, 144 bytes, 256-byte aligned.
This is why "any activation" works and PReLU (per-channel) doesn't.

## Clocking

### PLL structure

| PLL | Drives | Notes |
|---|---|---|
| PLL0 | ACLK → CPU, SRAM0/1, peripherals | ACLK = PLL0 / ((div+1) × 2) |
| **PLL1** | **KPU and AI SRAM** | Independent from CPU clock |
| PLL2 | I2S / APU | |

All PLLs from 26 MHz reference. VCO range 350–1750 MHz.

**Critical: ACLK = PLL0 / 2 with default divider.** A 400 MHz CPU means
PLL0 = 800 MHz. An 800 MHz CPU means PLL0 = 1.6 GHz (within 1.75 GHz VCO
ceiling but barely).

### KPU clock is independent

```
KPU_freq = PLL1 / (AI_divider)    divider 1–16
```

You can raise KPU clock without touching the CPU clock. Sipeed's defaults
often leave the KPU at **300 MHz**, below the 400 MHz the TOPS number assumes.
Raising KPU clock via PLL1 is the lower-risk performance knob.

### Overclocking practical limits

- 400 MHz: qualified nominal, vendor-backed
- Up to 600 MHz: what MaixPy allows, board-dependent
- 800 MHz: requires voltage modification (VDD fixed at 0.9V on all boards)
- No thermal throttling mechanism exists in silicon

## Efficiency analysis

Measured: MobileNet-0.5 + YOLOv2 at 224×224 runs at ~18 FPS (56 ms/frame).
At ~200 MMAC/frame × 18 FPS = ~3.6 GMAC/s against 230 GMAC/s peak = **~1.5%
utilization**.

Root causes, in order:
1. 1×1 convolutions waste 8/9 taps (the MobileNet killer)
2. Depthwise convolutions have Cin=1, accumulator loop length 1
3. Lane underutilization from non-optimal widths (224 → 87.5%)
4. CPU fallback layers (softmax, concat, resize, YOLO decode)
5. V4 kmodel runs most ops in software
6. Feature maps exceeding 2 MiB force host round-trips

## Hard limits consolidated

| Limit | Value |
|---|---|
| Feature-map memory | 2 MiB (15-bit × 64B addressing, exactly saturated) |
| Weights real-time | ~5.5 MiB |
| Channels | 1–1024 (BN RAM depth 1056 confirms) |
| Input size (nncase) | 4×4 to 512×256 |
| Practical input | ≤ 320×240 |
| Kernels | 1×1 and 3×3 only (66-byte RAM halo is ±1) |
| Stride | 1 natively; 2 via conv + left_top pooling |
| Weight loads/layer | ≤ 64 (6-bit field) |
| Per-load weight | 30 KiB (nncase; TRM says 36 or 72 — contested) |
| Layer queue depth | 13 layers |
| Depthwise | 3×3 stride-1 only |
| Network depth | No hardware limit |
