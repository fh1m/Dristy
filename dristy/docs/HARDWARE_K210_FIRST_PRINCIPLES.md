# The K210 from first principles — what the silicon can actually do

Working reference for the Dristy firmware. Everything here is either measured on
our own unit, read out of the Kendryte Standalone SDK source we have checked
out locally, or sourced from primary documentation. Claims are tagged:

- **[MEASURED]** — observed on our SEN0305 SEN0305.
- **[SDK]** — read directly from `_deps/kendryte-standalone-sdk`.
- **[DOC]** — Kendryte datasheet, nncase docs, or programming guide.
- **[REPORTED]** — third party, useful but unverified by us.

---

## 1. The chip

| Property | Value | Source |
|---|---|---|
| CPU | 2x RISC-V RV64GC, in-order, with double-precision FPU | [DOC] |
| Default CPU clock | 400 MHz nominal; PLL0 780 MHz with ACLK = PLL0/2 = 390 MHz in the Linux clock driver | [DOC] |
| Practical clock range | 40–600 MHz per NuttX; 800 MHz is an unverified overclock claim | [REPORTED] |
| SRAM | 8 MiB total: **4 MiB SRAM0 + 2 MiB SRAM1** + 2 MiB AI SRAM (asymmetric, not 3+3) | [DOC/TRM] |
| AI SRAM address | `0x40600000`–`0x407FFFFF` (cached alias); `0x80…` uncached alias exists | [DOC] |
| KPU | Fixed-function CNN accelerator, 64 KLUs, 576-bit SIMD datapath | [REPORTED] |
| KPU throughput | 0.25 TOPS nominal, ~0.5 TOPS overclocked | [REPORTED] |
| Flash (our unit) | 16 MiB Winbond, JEDEC `ef 60 18` | [MEASURED] |
| Camera (our unit) | OV2640, SCCB `0x60`, mid `0x7FA2`, pid `0x2642` | [MEASURED] |

### Clocking is coupled — this bites

The first two SRAM banks are clocked from ACLK (the CPU clock domain). The
third bank — the 2 MiB AI SRAM — is clocked from **PLL1**. The Linux clock
driver comments state plainly that PLL1 must be set so the AI SRAM runs at the
same rate as the other banks, and that mismatched rates **hang the board**.

Practical consequence: overclocking is not a single knob. Raising the CPU clock
without moving PLL1 in step will hang or corrupt AI SRAM. Any Dristy
overclocking feature must move PLL0 and PLL1 together and be validated with a
KPU workload, not just a CPU benchmark.

---

## 2. The KPU, at the register level

The KPU is **not** a programmable MAC array or a systolic array (despite what
several secondary sources claim). It is a fixed-function, output-stationary
SIMD engine with 64 parallel lanes. Software pushes twelve 64-bit words into a
layer argument FIFO (up to 13 layers deep) and the hardware executes one
complete fused convolution→batchnorm→activation→pooling stage.

The feature-map RAM is physically **66 bytes wide** (not 64): indices 1–64 hold
pixel data, index 0 replicates byte 64 of the previous row, index 65 replicates
byte 1 of the next row. This **±1 halo is hardwired** and is the fundamental
reason only 1×1 and 3×3 kernels exist — a 5×5 kernel would need a ±2 halo and
a 68-byte RAM. Each clock cycle, 64 bytes are read and fanned out as 64
overlapping 3-wide windows into the 64 ALUs, yielding **64 × 9 = 576
MACs/cycle** sustained for 3×3 convolution.

**1×1 convolutions use only 1 of 9 taps**, dropping to 64 MACs/cycle — a 9×
throughput cliff. Since MobileNet spends most work in 1×1 pointwise layers,
this is the main reason real MobileNet inference lands at ~1.5% of peak TOPS.

This is `kpu_layer_argument_t` from `lib/drivers/include/kpu.h` [SDK]. The field
widths are the real hardware limits, and several are not documented anywhere
else:

| Register | Field | Bits | What it means |
|---|---|---|---|
| `interrupt_enabe` | `int_en` | 1 | completion interrupt |
| | `ram_flag` | 1 | which AI SRAM half (ping-pong) |
| | `full_add` | 1 | **accumulate into destination — residual/skip connections in hardware** |
| | `depth_wise_layer` | 1 | **depthwise convolution in hardware** |
| `image_addr` | `image_src_addr` | 15 | source, in 64-byte lines |
| | `image_dst_addr` | 15 | destination, in 64-byte lines |
| `image_channel_num` | `i_ch_num` / `o_ch_num` | 10 / 10 | **max 1024 channels each** |
| | `o_ch_num_coef` | 10 | output channels per coefficient group |
| `image_size` | `i_row_wid` / `o_row_wid` | 10 / 10 | **max width 1024** |
| | `i_col_high` / `o_col_high` | 9 / 9 | **max height 512** |
| `kernel_pool_type_cfg` | `kernel_type` | 3 | 0 = 1x1, 1 = 3x3 |
| | `pad_type`, `pad_value` | 1, 8 | padding mode and fill value |
| | `pool_type` | 4 | 16 pooling modes (see below) |
| | `bypass_conv` | 1 | pooling/activation only, no convolution |
| | `dma_burst_size` | 8 | DMA burst tuning |
| | `bwsx_base_addr` | 32 | batch-norm parameter table address |
| `kernel_load_cfg` | `para_start_addr`, `para_size` | 32, 17 | weight location and size |
| | `load_coor`, `load_time` | 1, 6 | weight reload scheduling |
| `kernel_calc_type_cfg` | `channel_switch_addr` | 15 | channel stride, 64-byte lines |
| | `row_switch_addr` | 4 | row stride, 64-byte lines |
| | `coef_size`, `coef_group` | 8, 3 | coefficient grouping |
| | `active_addr` | 32 | **activation lookup table address** |
| `write_back_cfg` | `wb_*` | 15/4/3 | output strides |
| `conv_value` | `shr_w`, `shr_x`, `arg_w`, `arg_x` | 4/4/24/24 | requantization shifts and multipliers |
| `conv_value2` | `arg_add` | 40 | bias term = k*k*bw_div_sw*bx_div_sx |
| `dma_parameter` | `send_data_out`, `channel_byte_num`, `dma_total_byte` | 1/16/32 | output DMA |

### What the register widths prove

`image_src_addr` is 15 bits addressing 64-byte lines: 2^15 x 64 = **exactly
2 MiB**. This confirms two things at once — the AI SRAM size, and that *every*
feature map is laid out in 64-byte lines. That 64-byte granularity is the
origin of the padding rules that make odd channel counts and widths wasteful.

`full_add` and `depth_wise_layer` being hardware bits is the most important
architectural finding for model selection: **residual connections and depthwise
separable convolutions are free**, not CPU fallbacks. MobileNet-style backbones
with skip connections are the natural fit for this silicon.

### Arithmetic

8-bit multiplies accumulating into **64-bit** accumulators, then requantized
through a shift/multiply stage (`shr_w`/`arg_w`, `shr_x`/`arg_x`, `arg_add`),
then batch-normalized, then passed through a **16-segment piecewise-linear
activation table** at `active_addr`. Because activation is a table, *any*
elementwise activation shape can be approximated — this is why the docs say
"any form of activation function". PReLU is the documented exception, since it
is channel-parametric rather than elementwise.

### Pooling modes (`pool_type`) [DOC]

`0` bypass, `1` max 2x2 s2, `2` mean 2x2 s2, `3` max 4x4 s4, `4` mean 4x4 s4,
`5` left-top 2x2 s2, `6` right-top 2x2 s2, `7` left-top 4x4 s4, `8` mean 2x2 s1,
`9` max 2x2 s1, and further variants. Note `8`/`9` are **stride-1** pooling,
which is useful for keeping resolution while smoothing.

---

## 3. The operator contract — what compiles and what does not

This is the authoritative nncase constraint set [DOC]. Violating any of it
means the layer silently drops to CPU (slow) or fails to compile.

**Fully KPU-accelerated** — all of these must hold:

- Input feature map **≤ 320x240 (WxH)**
- Output feature map **≥ 4x4 (WxH)**
- Channels **1 to 1024**
- **Same symmetric** padding
- `Conv2D` or `DepthwiseConv2D`, kernel **1x1 or 3x3**, stride **1 or 2**
- `MaxPool` / `AveragePool` of **2x2 or 4x4**
- Any **elementwise** activation (ReLU, ReLU6, LeakyReLU, Sigmoid, …)

**Partially accelerated** — works, but nncase inserts costly helper ops:

| Original | Becomes |
|---|---|
| asymmetric / valid padding | `Pad` + conv + `Crop` |
| stride not 1 or 2 | `KPUConv2D` + `StridedSlice` |
| `MatMul` / Dense | `Pad`→4x4 + `KPUConv2D` 1x1 + `Crop`→1x1 |
| `DilatedConv2D` | `SpaceToBatch` + `KPUConv2D` + `BatchToSpace` |
| `TransposeConv2D` | `Pad` + `KPUConv2D` |

**Not supported on KPU:** `PReLU`. Kernels other than 1x1/3x3. Anything
requiring general matrix math outside the conv formulation.

**Memory rule:** input and output feature maps live in the 2 MiB AI SRAM and
**each individual layer** must fit. Weights live in the 6 MiB general RAM.
Real-time weight budget is ~5.9 MiB; beyond that you are streaming from flash
and leave real-time behind.

### kmodel V3 versus V4 — V3 is the right choice

A crucial and widely misunderstood point [DOC]:

- **V3** (nncase v0.1.0-rc5): fewer supported operators, but **smaller code,
  less memory, and genuinely hardware-accelerated**.
- **V4** (nncase v0.2.0): more operators, but the extra ones are **software
  implemented with no hardware acceleration**, and memory use rises sharply.

Sipeed's own guidance is to use V3 whenever the operator set permits, and reach
for V4 only when a required operator is missing. Our pinned toolchain is
v0.1.0-rc5 producing kmodel V3, and that is the correct default, not a legacy
constraint. There is also a documented V3 gotcha: a `conv` with `stride != 1`
needs an explicit `ZeroPadding` layer or V3 conversion errors outright.

---

## 4. The camera path — and the zero-copy trick

| Property | Value | Source |
|---|---|---|
| DVP maximum | **640x480**, a hardware limit, not a memory limit | [DOC] |
| KPU network input maximum | **320x256** | [DOC] |
| Formats | RGB565, YUV422, single-channel Y, RGB888 planar | [DOC] |
| Our measured rate | 30.0 fps stock at 320x240; 19–25 fps in the Dristy camera app | [MEASURED] |
| Reported VGA rate | ~10 fps at 640x480, ~15 fps at 320x240 in other projects | [REPORTED] |

The DVP writes **two destinations simultaneously**:

```c
dvp_set_output_attributes(dvp, DATA_FOR_DISPLAY, VIDEO_FMT_RGB565,       lcd_buffer);
dvp_set_output_attributes(dvp, DATA_FOR_AI,      VIDEO_FMT_RGB24_PLANAR, (void *)0x40600000);
```

The AI output goes straight into KPU SRAM in the planar RGB888 layout the KPU
wants. **No CPU touches the pixels between sensor and accelerator.** Any design
that copies or converts frames on the CPU before inference is leaving most of
the machine's throughput unused.

The display path and the AI path can also run at different formats from the
same capture, which is how you show a 320x240 preview while feeding the network
whatever resolution it needs.

---

## 5. Where the performance actually comes from

The published Kapernikov case study is the clearest evidence of the achievable
ceiling [REPORTED]. Their K210 YOLO pipeline, written naively, was fully
sequential: camera, then CPU pre-processing, then KPU inference, then CPU
post-processing, with every stage idle while another worked.

Their staged optimization:

1. **Overlap camera with compute** — process frame N while frame N+1 is being
   captured. Cost: one extra ~147 KB buffer. Gain: 41 ms per frame.
2. **Move inference to the second core** — cost another ~147 KB. Gain: 10 ms.
3. **Full pipelining** across camera / core0 / KPU / core1.

Final result: **35 ms throughput (~28 fps)**, against a camera hard floor of
33 ms. Latency stayed at ~90 ms — pipelining trades latency for throughput.
Total extra memory ~850 KB, about 14% of the 6 MiB.

Two lessons for Dristy:

- The win comes from **concurrency, not from a faster model**. Same network,
  roughly 4x the frame rate.
- **Throughput and latency are separate dials.** A drone doing visual servoing
  cares about latency; a counting application cares about throughput. Dristy
  should expose both and let the integrator choose.

Note also that KPU access is **exclusive** — it processes one layer stream at a
time. Two cores cannot both drive it. Core 1 should run post-processing,
tracking, and protocol work, not a second inference.

---

## 6. Classical CV — the non-neural half

Sipeed's published comparison table gives K210 figures [REPORTED]:

| Operation | K210 at 320x240 | Notes |
|---|---|---|
| `find_blobs` (colour blobs, LAB thresholds) | **8.8 ms (~114 fps)** | the workhorse for colour and line following |
| `find_blobs` at 640x480 | not supported | memory |
| `find_contours` | not available on K210 | available on later Maix parts |

Colour blob detection at 114 fps is dramatically faster than any neural path
and is the correct tool for colour tracking and line following. Line following
is conventionally `get_regression` over a thresholded binary image.

Honest assessment of the harder classical algorithms, to be verified on-device
rather than assumed:

- **Optical flow**: sparse Lucas-Kanade on a small number of corners is
  plausible on core 1; dense flow is not. No authoritative K210 benchmark found
  yet — this needs our own measurement.
- **Tracking**: IoU/centroid association with a Kalman filter is cheap and is
  the right first implementation. KCF and correlation-filter trackers are not
  documented on K210 and would need FFT work the chip has hardware for (there
  is a hardcore FFT unit) but no published integration.
- **Segmentation**: a small U-Net is bounded by the 2 MiB feature-map limit and
  the ≥4x4 output rule. Low-resolution segmentation may fit; full-resolution
  will not.

The K210 also has **hardcore FFT, SHA256 and an audio accelerator** that are
entirely unused by both the stock firmware and Dristy. The FFT unit in
particular is an unexploited asset for correlation-based tracking and for
frequency-domain features.

---

## 7. Consolidated design rules for Dristy

Derived from everything above. These are the constraints any Dristy vision
pipeline must respect:

1. Network input must be **≤ 320x240**, ideally with width a multiple of 64
   bytes' worth of channels to avoid line padding waste.
2. Use **1x1 and 3x3 convolutions only**, stride 1 or 2, symmetric padding.
   Design the network to the hardware, do not hope the compiler rescues it.
3. Prefer **depthwise separable blocks with residual connections** — both are
   hardware features.
4. Avoid `PReLU`. Any other elementwise activation is free via the 16-segment
   table.
5. Keep every **single layer's** feature maps under 2 MiB, and total weights
   under ~5.9 MiB for real-time.
6. Target **kmodel V3**. Treat V4 as an escape hatch that costs hardware
   acceleration.
7. Feed the KPU **directly from the DVP** in RGB888 planar at `0x40600000`.
   Never convert frames on the CPU.
8. **Pipeline across both cores**: core 0 owns capture and KPU dispatch, core 1
   owns post-processing, tracking and the link protocol. Budget ~850 KB for
   buffers.
9. Colour and line work belongs in **classical CV at ~114 fps**, not in a
   neural network.
10. Overclocking means moving **PLL0 and PLL1 together**, validated under KPU
    load.

---

## 8. Complete pin map (from board.toml) [SDK]

Source: `boards/sen0305/board.toml` in Dristy.

### Display (ST7789V, 320x240 IPS, SPI0 at 40 MHz)

| Function | K210 IO | FPIOA function | Peripheral |
|---|---|---|---|
| LCD backlight | 24 | timer0-toggle1 | timer0 |
| LCD DC | 18 | gpiohs15 | gpiohs |
| LCD CS | 19 | spi0-ss3 | spi0 |
| LCD SCLK | 20 | spi0-sclk | spi0 |
| LCD MOSI | 21 | spi0-d0 | spi0 |
| LCD reset | 22 | gpiohs14 | gpiohs |

### Camera (OV2640, DVP, XCLK 40 MHz, SCCB 100 kHz)

| Function | K210 IO | FPIOA function |
|---|---|---|
| PCLK | 47 | cmos-pclk |
| XCLK | 46 | cmos-xclk |
| HREF | 45 | cmos-href |
| PWDN | 44 | cmos-pwdn |
| VSYNC | 43 | cmos-vsync |
| RST | 42 | cmos-rst |
| SCCB SCL | 40 | sccb-sclk |
| SCCB SDA | 41 | sccb-sda |

### LEDs (Timer2 PWM at 20 kHz)

| Function | K210 IO | PWM channel |
|---|---|---|
| Illumination (white) | 23 | timer2-ch4 |
| RGB Red | 32 | timer2-ch1 |
| RGB Green | 30 | timer2-ch2 |
| RGB Blue | 31 | timer2-ch3 |

### Buttons (GPIOhs, active-low assumed)

| Function | K210 IO | GPIOhs |
|---|---|---|
| Left | 39 | gpiohs2 |
| OK | 38 | gpiohs3 |
| Right | 37 | gpiohs4 |
| Back | 36 | gpiohs5 |

### SD card (SPI mode, SPI1)

| Function | K210 IO | FPIOA function |
|---|---|---|
| SCLK | 27 | spi1-sclk |
| D0 (MOSI/MISO) | 28 | spi1-d0 |
| D1 | 26 | spi1-d1 |
| CS | 29 | gpiohs6 |

SD is present on SEN0305 hardware and available in SPI mode. This is a model
storage path: models can be loaded from SD at runtime without reflashing.

### Gravity connector (runtime-muxed, IO34/IO35)

The four-pin Gravity connector shares two data pins that can be dynamically
reassigned between UART1 and I2C0 at runtime:

| Mode | Function | K210 IO | FPIOA function |
|---|---|---|---|
| UART | RX | 34 | uart1-rx |
| UART | TX | 35 | uart1-tx |
| I2C | SCL | 34 | i2c0-sclk |
| I2C | SDA | 35 | i2c0-sda |

USB serial (for flashing and host link) is on UART3, IO4/IO5.

### Flash (SPI3)

| Property | Value |
|---|---|
| SPI peripheral | spi3 |
| CS | 0 |
| Mode | QIO |
| Boot baud | 115200 |
| Flash baud | 2000000 |

---

## 9. Open questions to resolve by measurement

These are not yet answered and should be settled on hardware rather than
guessed:

- Actual KPU inference time for the 20-class YOLOv2 kmodel on our unit.
- Whether our OV2640 can be driven above 30 fps at reduced resolution, and what
  the true DVP/sensor frame floor is.
- Real sustained memory bandwidth to AI SRAM versus general SRAM.
- Whether this board is stable at 500–600 MHz with PLL1 tracking, under KPU
  load, and what it does to inference time.
- Sparse optical flow cost per frame on core 1.
- Whether the hardcore FFT unit is reachable and useful for tracking.
