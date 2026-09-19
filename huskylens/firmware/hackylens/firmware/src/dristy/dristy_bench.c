#include "dristy_bench.h"

#include <stdio.h>
#include <string.h>

#include "hal_time.h"

/* ---------------------------------------------------------------------------
 * RISC-V cycle counter
 *
 * K210 provides mcycle CSR for cycle-accurate timing. At 400 MHz,
 * 1 cycle = 2.5 ns. We read it directly via inline asm for minimal overhead.
 *
 * When profiling is disabled, all bench calls are effectively no-ops
 * (checked via g_enabled flag).
 * ------------------------------------------------------------------------- */

static inline uint64_t read_cycles(void)
{
    /* mcycle CSR traps as illegal on this firmware build; use monotonic us. */
    return (uint64_t)hal_time_us() * 400ULL;
}

/* Approximate CPU MHz (set during init by measuring against hal_time) */
static uint32_t g_cpu_mhz = 400;

static inline uint32_t cycles_to_us(uint64_t cycles)
{
    return (uint32_t)(cycles / g_cpu_mhz);
}

/* ---------------------------------------------------------------------------
 * State
 * ------------------------------------------------------------------------- */

static dristy_bench_report_t g_report;
static uint8_t g_enabled;

/* Per-stage start cycle (only valid between start/stop) */
static uint64_t g_start_cycles[DRISTY_BENCH_STAGE_COUNT];

/* FPS tracking */
static uint64_t g_fps_window_start;
static uint32_t g_fps_window_frames;

/* Frame-level totals for utilisation */
static uint64_t g_frame_start_cycle;
static uint64_t g_kpu_cycles_this_frame;
static uint64_t g_kpu_cycles_total;
static uint64_t g_total_cycles_total;

/* Stage names */
static const char *g_stage_names[DRISTY_BENCH_STAGE_COUNT] = {
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
};

/* Histogram bucket boundaries in µs */
static const uint32_t g_hist_boundaries[7] = {
    100, 500, 1000, 2000, 5000, 10000, 50000
};

static void update_stats(dristy_bench_stats_t *s, uint32_t us)
{
    s->count++;
    s->last_us = us;
    s->sum_us += us;
    if(us < s->min_us || s->count == 1) s->min_us = us;
    if(us > s->max_us) s->max_us = us;

    /* Histogram bucket */
    uint8_t bucket = 7;
    for(uint8_t i = 0; i < 7; i++)
    {
        if(us < g_hist_boundaries[i])
        {
            bucket = i;
            break;
        }
    }
    s->hist[bucket]++;
}

/* Estimate p95 from histogram */
static uint32_t estimate_p95(const dristy_bench_stats_t *s)
{
    if(s->count == 0) return 0;

    uint32_t target = (s->count * 95 + 99) / 100;
    uint32_t cumulative = 0;

    for(uint8_t i = 0; i < 8; i++)
    {
        cumulative += s->hist[i];
        if(cumulative >= target)
        {
            if(i == 0) return g_hist_boundaries[0];
            if(i >= 7) return s->max_us;
            return g_hist_boundaries[i];
        }
    }
    return s->max_us;
}

/* ---------------------------------------------------------------------------
 * Calibrate CPU clock by measuring cycles vs hal_time_us
 * ------------------------------------------------------------------------- */

static void calibrate_clock(void)
{
    g_cpu_mhz = 400U;
}

/* ---------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void dristy_bench_init(void)
{
    memset(&g_report, 0, sizeof(g_report));
    memset(g_start_cycles, 0, sizeof(g_start_cycles));
    g_enabled = 0;
    g_fps_window_start = 0;
    g_fps_window_frames = 0;
    g_kpu_cycles_this_frame = 0;
    g_kpu_cycles_total = 0;
    g_total_cycles_total = 0;

    calibrate_clock();
    printf("[DRISTY] bench init, CPU @ %lu MHz\r\n", (unsigned long)g_cpu_mhz);
}

void dristy_bench_enable(uint8_t enabled)
{
    if(enabled && !g_enabled)
    {
        dristy_bench_reset();
        g_fps_window_start = hal_time_us();
    }
    g_enabled = enabled;
    printf("[DRISTY] bench %s\r\n", enabled ? "ENABLED" : "DISABLED");
}

uint8_t dristy_bench_enabled(void)
{
    return g_enabled;
}

void dristy_bench_start(dristy_bench_stage_t stage)
{
    if(!g_enabled || stage >= DRISTY_BENCH_STAGE_COUNT) return;

    g_start_cycles[stage] = read_cycles();

    if(stage == DRISTY_BENCH_FRAME_TOTAL)
        g_frame_start_cycle = g_start_cycles[stage];
}

void dristy_bench_stop(dristy_bench_stage_t stage)
{
    if(!g_enabled || stage >= DRISTY_BENCH_STAGE_COUNT) return;

    uint64_t end = read_cycles();
    uint64_t elapsed = end - g_start_cycles[stage];
    uint32_t us = cycles_to_us(elapsed);

    update_stats(&g_report.stages[stage], us);

    /* Track KPU utilisation */
    if(stage == DRISTY_BENCH_KPU_INFERENCE)
        g_kpu_cycles_this_frame += elapsed;
}

void dristy_bench_record_drop(void)
{
    if(g_enabled)
        g_report.system.frames_dropped++;
}

void dristy_bench_frame_end(void)
{
    if(!g_enabled) return;

    uint64_t now = read_cycles();
    uint64_t frame_cycles = now - g_frame_start_cycle;

    g_kpu_cycles_total += g_kpu_cycles_this_frame;
    g_total_cycles_total += frame_cycles;
    g_kpu_cycles_this_frame = 0;

    g_report.system.frames_profiled++;

    /* FPS */
    g_fps_window_frames++;
    uint64_t now_us = hal_time_us();
    uint64_t elapsed = now_us - g_fps_window_start;
    if(elapsed >= 1000000ULL)
    {
        g_report.system.fps_x10 =
            (uint16_t)(g_fps_window_frames * 10000000ULL / elapsed);
        g_fps_window_start = now_us;
        g_fps_window_frames = 0;
    }

    /* Utilisation */
    if(g_total_cycles_total > 0)
    {
        g_report.system.kpu_util_x10 =
            (uint16_t)(g_kpu_cycles_total * 1000 / g_total_cycles_total);
    }
}

const dristy_bench_report_t *dristy_bench_report(void)
{
    return &g_report;
}

void dristy_bench_reset(void)
{
    memset(&g_report, 0, sizeof(g_report));
    g_kpu_cycles_total = 0;
    g_total_cycles_total = 0;
    g_fps_window_frames = 0;
    g_fps_window_start = hal_time_us();
}

void dristy_bench_print_report(void)
{
    const dristy_bench_report_t *r = &g_report;

    printf("\r\n");
    printf("╔══════════════════════════════════════════════════════╗\r\n");
    printf("║           DRISTY BENCHMARK REPORT                   ║\r\n");
    printf("╠══════════════════════════════════════════════════════╣\r\n");
    printf("║ CPU: %lu MHz  Frames: %lu  FPS: %u.%u              \r\n",
           (unsigned long)g_cpu_mhz,
           (unsigned long)r->system.frames_profiled,
           r->system.fps_x10 / 10, r->system.fps_x10 % 10);
    printf("║ Drops: %lu  KPU util: %u.%u%%                      \r\n",
           (unsigned long)r->system.frames_dropped,
           r->system.kpu_util_x10 / 10, r->system.kpu_util_x10 % 10);
    printf("╠══════════════════════════════════════════════════════╣\r\n");
    printf("║ Stage            │  Min   │  Mean  │  Max   │  P95  \r\n");
    printf("╠──────────────────┼────────┼────────┼────────┼───────\r\n");

    for(int i = 0; i < DRISTY_BENCH_STAGE_COUNT; i++)
    {
        const dristy_bench_stats_t *s = &r->stages[i];
        if(s->count == 0) continue;

        uint32_t mean_us = (uint32_t)(s->sum_us / s->count);
        uint32_t p95_us = estimate_p95(s);

        printf("║ %-16s │ %5lu  │ %5lu  │ %5lu  │ %5lu \r\n",
               g_stage_names[i],
               (unsigned long)s->min_us,
               (unsigned long)mean_us,
               (unsigned long)s->max_us,
               (unsigned long)p95_us);
    }

    printf("╚══════════════════════════════════════════════════════╝\r\n");

    /* Theoretical ceilings */
    printf("\r\n--- Theoretical Ceilings (K210 @ %lu MHz) ---\r\n", (unsigned long)g_cpu_mhz);
    printf("  KPU peak:        %.1f TOPS\r\n",
           DRISTY_CEIL_KPU_TOPS * (float)g_cpu_mhz / 400.0f);
    printf("  DVP max:         %d FPS (OV2640 320x240)\r\n", DRISTY_CEIL_DVP_MAX_FPS);
    printf("  LCD refresh:     %d ms (SPI @ %d MHz)\r\n",
           DRISTY_CEIL_LCD_REFRESH_MS, DRISTY_CEIL_LCD_SPI_MHZ);
    printf("  HW FFT 512pt:    %d us\r\n", DRISTY_CEIL_FFT_512PT_US);
    printf("  UART1 max:       %d baud (%d KB/s)\r\n",
           DRISTY_CEIL_UART1_MAX_BAUD, DRISTY_CEIL_UART1_MAX_BAUD / 10 / 1024);

    /* Pipeline bottleneck analysis */
    if(r->stages[DRISTY_BENCH_FRAME_TOTAL].count > 0)
    {
        uint32_t total_mean = (uint32_t)(r->stages[DRISTY_BENCH_FRAME_TOTAL].sum_us /
                                          r->stages[DRISTY_BENCH_FRAME_TOTAL].count);
        printf("\r\n--- Bottleneck Analysis ---\r\n");
        printf("  Frame budget:    %lu us (%.1f FPS)\r\n",
               (unsigned long)total_mean,
               total_mean > 0 ? 1000000.0f / total_mean : 0);

        /* Find the dominant stage */
        uint32_t max_mean = 0;
        int max_stage = -1;
        for(int i = 1; i < DRISTY_BENCH_STAGE_COUNT; i++)
        {
            if(r->stages[i].count == 0) continue;
            uint32_t mean = (uint32_t)(r->stages[i].sum_us / r->stages[i].count);
            if(mean > max_mean)
            {
                max_mean = mean;
                max_stage = i;
            }
        }
        if(max_stage >= 0)
        {
            float pct = total_mean > 0 ? (float)max_mean * 100.0f / total_mean : 0;
            printf("  Bottleneck:      %s (%.1f%% of frame)\r\n",
                   g_stage_names[max_stage], pct);
        }

        /* Headroom */
        uint32_t target_budget = 33333; /* 30 FPS target */
        if(total_mean < target_budget)
        {
            printf("  Headroom:        %lu us (%.0f%% spare for 30 FPS)\r\n",
                   (unsigned long)(target_budget - total_mean),
                   (float)(target_budget - total_mean) * 100.0f / target_budget);
        }
        else
        {
            printf("  OVER BUDGET:     %lu us over 30 FPS target\r\n",
                   (unsigned long)(total_mean - target_budget));
        }
    }

    printf("\r\n");
}

uint16_t dristy_bench_serialize(uint8_t *buf, uint16_t buf_size)
{
    if(buf_size < 8) return 0;

    uint16_t off = 0;

    /* Header: magic + frame count + FPS */
    buf[off++] = 'B';
    buf[off++] = 'M';
    buf[off++] = (uint8_t)(g_report.system.frames_profiled & 0xFF);
    buf[off++] = (uint8_t)((g_report.system.frames_profiled >> 8) & 0xFF);
    buf[off++] = (uint8_t)(g_report.system.fps_x10 & 0xFF);
    buf[off++] = (uint8_t)((g_report.system.fps_x10 >> 8) & 0xFF);
    buf[off++] = (uint8_t)(g_report.system.kpu_util_x10 & 0xFF);
    buf[off++] = (uint8_t)((g_report.system.kpu_util_x10 >> 8) & 0xFF);

    /* Per-stage: 8 bytes each (mean_us:u16, max_us:u16, p95_us:u16, count:u16) */
    for(int i = 0; i < DRISTY_BENCH_STAGE_COUNT && off + 8 <= buf_size; i++)
    {
        const dristy_bench_stats_t *s = &g_report.stages[i];
        uint16_t mean = s->count > 0 ? (uint16_t)(s->sum_us / s->count) : 0;
        uint16_t max_v = s->max_us > 65535 ? 65535 : (uint16_t)s->max_us;
        uint16_t p95 = (uint16_t)estimate_p95(s);
        uint16_t cnt = s->count > 65535 ? 65535 : (uint16_t)s->count;

        buf[off++] = mean & 0xFF;
        buf[off++] = (mean >> 8) & 0xFF;
        buf[off++] = max_v & 0xFF;
        buf[off++] = (max_v >> 8) & 0xFF;
        buf[off++] = p95 & 0xFF;
        buf[off++] = (p95 >> 8) & 0xFF;
        buf[off++] = cnt & 0xFF;
        buf[off++] = (cnt >> 8) & 0xFF;
    }

    return off;
}

const char *dristy_bench_stage_name(dristy_bench_stage_t stage)
{
    if(stage >= DRISTY_BENCH_STAGE_COUNT) return "?";
    return g_stage_names[stage];
}
