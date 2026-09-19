#ifndef DRISTY_BENCH_H
#define DRISTY_BENCH_H

#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Dristy Benchmark & Profiler
 *
 * Cycle-accurate timing of every pipeline stage. Results are accumulated
 * over N frames, then reported as min/max/mean/p95 via DLP or UART printf.
 *
 * K210 timing source: RISC-V mcycle CSR @ CPU clock (400-600 MHz).
 * At 400 MHz: 1 cycle = 2.5 ns. 32-bit overflow at ~10.7 seconds.
 * We use 64-bit counters for safety.
 *
 * Stages profiled:
 *   0. FRAME_TOTAL     — entire pipeline tick
 *   1. DVP_CAPTURE     — camera frame acquisition
 *   2. KPU_INFERENCE   — neural network forward pass
 *   3. YOLO_DECODE     — YOLO post-processing (anchor decode)
 *   4. NMS             — non-maximum suppression
 *   5. TRACKER_UPDATE  — DeepSORT track update (predict + associate)
 *   6. APRILTAG        — AprilTag detector (if active)
 *   7. ARUCO           — ArUco detector (if active)
 *   8. MOTION          — motion detection (if active)
 *   9. FLOW            — optical flow (if active)
 *  10. COLOUR          — colour blob detection (if active)
 *  11. QR              — QR decode (if active)
 *  12. POSE            — PnP pose estimation
 *  13. TARGET_SELECT   — primary target selection
 *  14. RESULT_BUS      — result bus commit
 *  15. DLP_SERIALIZE   — protocol serialisation
 *  16. LCD_UPDATE      — display refresh
 *  17. IDLE            — time spent idle (waiting for next frame)
 *
 * Usage in pipeline code:
 *   dristy_bench_start(DRISTY_BENCH_KPU_INFERENCE);
 *   ... run KPU ...
 *   dristy_bench_stop(DRISTY_BENCH_KPU_INFERENCE);
 *
 * Memory model optimisations profiled:
 *   - Zero-copy DVP→KPU path utilisation %
 *   - SRAM bandwidth saturation %
 *   - KPU utilisation % (cycles busy / cycles total)
 *   - Core 1 utilisation %
 * ------------------------------------------------------------------------- */

/* Pipeline stages to profile */
typedef enum
{
    DRISTY_BENCH_FRAME_TOTAL    = 0,
    DRISTY_BENCH_DVP_CAPTURE    = 1,
    DRISTY_BENCH_KPU_INFERENCE  = 2,
    DRISTY_BENCH_YOLO_DECODE    = 3,
    DRISTY_BENCH_NMS            = 4,
    DRISTY_BENCH_TRACKER        = 5,
    DRISTY_BENCH_APRILTAG       = 6,
    DRISTY_BENCH_ARUCO          = 7,
    DRISTY_BENCH_MOTION         = 8,
    DRISTY_BENCH_FLOW           = 9,
    DRISTY_BENCH_COLOUR         = 10,
    DRISTY_BENCH_QR             = 11,
    DRISTY_BENCH_POSE           = 12,
    DRISTY_BENCH_TARGET_SELECT  = 13,
    DRISTY_BENCH_RESULT_BUS     = 14,
    DRISTY_BENCH_DLP_SERIALIZE  = 15,
    DRISTY_BENCH_LCD_UPDATE     = 16,
    DRISTY_BENCH_IDLE           = 17,
    DRISTY_BENCH_STAGE_COUNT
} dristy_bench_stage_t;

/* Per-stage statistics */
typedef struct
{
    uint32_t count;         /* number of measurements */
    uint32_t min_us;
    uint32_t max_us;
    uint64_t sum_us;        /* for mean calculation */
    uint32_t last_us;       /* most recent measurement */
    /* Histogram: 8 buckets for percentile estimation */
    uint32_t hist[8];       /* [<0.1ms, <0.5ms, <1ms, <2ms, <5ms, <10ms, <50ms, >=50ms] */
} dristy_bench_stats_t;

/* System-level metrics */
typedef struct
{
    uint32_t frames_profiled;
    uint16_t fps_x10;
    uint16_t target_fps;        /* configured target FPS */

    /* Utilisation (0-1000 = 0-100.0%) */
    uint16_t kpu_util_x10;      /* KPU busy cycles / total cycles */
    uint16_t core0_util_x10;    /* core 0 busy / total */
    uint16_t core1_util_x10;    /* core 1 busy / total */
    uint16_t sram_bw_util_x10;  /* estimated SRAM bandwidth usage */

    /* Memory */
    uint32_t sram_free;         /* free general SRAM bytes */
    uint32_t ai_sram_used;      /* AI SRAM used by current model */
    uint32_t flash_model_size;  /* loaded model size */

    /* Pipeline health */
    uint32_t frames_dropped;    /* frames where KPU was still busy */
    uint32_t tracker_overflows; /* times tracker hit max tracks */
    uint32_t result_bus_overruns; /* times DLP couldn't read before overwrite */
} dristy_bench_system_t;

/* Complete benchmark report */
typedef struct
{
    dristy_bench_stats_t stages[DRISTY_BENCH_STAGE_COUNT];
    dristy_bench_system_t system;
} dristy_bench_report_t;

/* --- API ---------------------------------------------------------------- */

/* Initialise the profiler (call once at boot) */
void dristy_bench_init(void);

/* Enable/disable profiling (disabled = zero overhead) */
void dristy_bench_enable(uint8_t enabled);
uint8_t dristy_bench_enabled(void);

/* Start timing a stage (records start cycle) */
void dristy_bench_start(dristy_bench_stage_t stage);

/* Stop timing a stage (computes elapsed, updates stats) */
void dristy_bench_stop(dristy_bench_stage_t stage);

/* Record a frame drop (KPU was busy when new frame arrived) */
void dristy_bench_record_drop(void);

/* End-of-frame: updates system metrics, FPS counter */
void dristy_bench_frame_end(void);

/* Get the current report (accumulated since last reset) */
const dristy_bench_report_t *dristy_bench_report(void);

/* Reset all counters */
void dristy_bench_reset(void);

/* Print a human-readable report to UART (debug console) */
void dristy_bench_print_report(void);

/* Serialise report into a binary buffer for DLP transmission.
 * Returns bytes written. */
uint16_t dristy_bench_serialize(uint8_t *buf, uint16_t buf_size);

/* Get stage name string */
const char *dristy_bench_stage_name(dristy_bench_stage_t stage);

/* --- Theoretical ceiling constants -------------------------------------- */

/* K210 @ 400 MHz theoretical maximums */
#define DRISTY_CEIL_KPU_TOPS            0.8f    /* 0.8 TOPS peak */
#define DRISTY_CEIL_CPU_MHZ             400     /* default clock */
#define DRISTY_CEIL_CPU_MHZ_OC          600     /* overclocked */
#define DRISTY_CEIL_SRAM_BW_MBPS        1600    /* 6 MB SRAM, dual-port, ~1.6 GB/s */
#define DRISTY_CEIL_AI_SRAM_KB          2048    /* 2 MB AI SRAM */
#define DRISTY_CEIL_DVP_MAX_FPS         60      /* OV2640 max at 320×240 */
#define DRISTY_CEIL_KPU_INPUT_MAX_W     320
#define DRISTY_CEIL_KPU_INPUT_MAX_H     256
#define DRISTY_CEIL_LCD_SPI_MHZ         40      /* SPI0 to ST7789 */
#define DRISTY_CEIL_LCD_REFRESH_MS      8       /* 320×240×2 / (40MHz/8) */
#define DRISTY_CEIL_UART1_MAX_BAUD      921600
#define DRISTY_CEIL_FFT_512PT_US        16      /* hardware FFT unit */

#endif /* DRISTY_BENCH_H */
