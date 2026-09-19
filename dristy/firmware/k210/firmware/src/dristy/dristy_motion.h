#ifndef DRISTY_MOTION_H
#define DRISTY_MOTION_H

#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Dristy Motion Detection
 *
 * Frame-differencing with adaptive background model. Detects moving regions
 * in the camera view without using the KPU.
 *
 * Use cases in robotics:
 *   - Intrusion/motion alert (security drone)
 *   - "Is something moving?" gate before expensive KPU inference
 *   - Obstacle motion detection for collision avoidance
 *   - Wake-from-sleep trigger (save power on stationary drone)
 *
 * Algorithm:
 *   1. Convert current frame to grayscale (1/4 resolution for speed)
 *   2. Absolute difference with previous frame
 *   3. Threshold → binary motion mask
 *   4. Morphological open (erode then dilate) to remove noise
 *   5. Connected component labelling → motion regions
 *   6. Report: total motion %, region count, largest region bbox
 *
 * Background model: exponential moving average (EMA) with configurable alpha.
 * This adapts to slow lighting changes but detects fast motion.
 *
 * Memory: ~38 KB for 160×120 (quarter-res) background + current + diff buffers
 * Speed: ~2 ms per frame at quarter resolution
 * ------------------------------------------------------------------------- */

#define DRISTY_MOTION_MAX_REGIONS 8

/* Motion region */
typedef struct
{
    int16_t x, y, w, h;        /* bounding box (in full-res coordinates) */
    uint32_t pixel_count;       /* number of motion pixels */
    float intensity;            /* average motion intensity (0..1) */
} dristy_motion_region_t;

/* Motion detection result */
typedef struct
{
    float motion_percent;       /* total frame area with motion (0..100) */
    float mean_intensity;       /* average diff intensity where motion (0..1) */
    uint8_t region_count;
    dristy_motion_region_t regions[DRISTY_MOTION_MAX_REGIONS];
    uint8_t triggered;          /* 1 if motion_percent > threshold */
} dristy_motion_result_t;

/* Configuration */
typedef struct
{
    uint8_t diff_threshold;     /* pixel diff threshold (0-255, default: 25) */
    float motion_threshold;     /* % of frame to trigger (default: 2.0) */
    float bg_alpha;             /* background adaptation rate (0.01-0.5, default: 0.05) */
    uint8_t erode_iterations;   /* morphological erode passes (default: 1) */
    uint8_t dilate_iterations;  /* morphological dilate passes (default: 2) */
    uint16_t min_region_area;   /* minimum motion pixels for a region (default: 50) */
    uint8_t quarter_res;        /* 1: process at 160×120, 0: full 320×240 */
} dristy_motion_config_t;

#define DRISTY_MOTION_CONFIG_DEFAULT {      \
    .diff_threshold     = 25,               \
    .motion_threshold   = 2.0f,             \
    .bg_alpha           = 0.05f,            \
    .erode_iterations   = 1,                \
    .dilate_iterations  = 2,                \
    .min_region_area    = 50,               \
    .quarter_res        = 1,                \
}

/* --- API ---------------------------------------------------------------- */

/* Initialise motion detector */
void dristy_motion_init(const dristy_motion_config_t *config);

/* Process a new grayscale frame. Returns 1 if motion detected. */
uint8_t dristy_motion_process(const uint8_t *gray,
                              uint16_t width, uint16_t height);

/* Get latest motion result */
const dristy_motion_result_t *dristy_motion_result(void);

/* Reset background model (e.g., after mode switch) */
void dristy_motion_reset_background(void);

/* Adjust threshold at runtime */
void dristy_motion_set_threshold(float percent);
void dristy_motion_set_sensitivity(uint8_t diff_thresh);

#endif /* DRISTY_MOTION_H */
