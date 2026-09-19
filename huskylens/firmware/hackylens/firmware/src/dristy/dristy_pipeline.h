#ifndef DRISTY_PIPELINE_H
#define DRISTY_PIPELINE_H

#include <stdint.h>

#include "dristy_modes.h"
#include "dristy_tracker.h"
#include "dristy_result_bus.h"

/* ---------------------------------------------------------------------------
 * Dristy Pipeline — The Central Nervous System
 *
 * Wires together:
 *   camera_stream → camera_ai_input → KPU → post-process → NMS → tracker
 *                                                                → result_bus
 *   camera_stream → grayscale copy → AprilTag/QR/flow → result_bus
 *
 * Designed for drone/rover/AUV integration:
 *
 * Frame flow (mailbox semantics):
 *   1. DVP captures newest frame into camera_stream slot (RGB565)
 *   2. Simultaneously, DVP writes RGB888 planar to AI SRAM (zero-copy)
 *   3. On FRAME_FINISH: if KPU idle → dispatch inference immediately
 *   4. On KPU_DONE: post-process on core 1, feed tracker, commit to bus
 *   5. For classical modes: RGB565→grayscale on core 1, run AprilTag/flow
 *   6. Result bus snapshot committed → DLP sends to host on next poll
 *
 * The pipeline is mode-aware: switching mode reconfigures which stages
 * are active without stopping the camera stream.
 *
 * Control output:
 *   - Primary target at result_bus.target — directly usable as PID error
 *   - Normalised coordinates [-1000, +1000] — resolution-independent
 *   - Timestamped for IMU fusion
 *   - Velocity estimates for derivative control
 * ------------------------------------------------------------------------- */

/* Pipeline configuration */
typedef struct
{
    dristy_mode_t initial_mode;
    uint16_t camera_width;      /* 320 */
    uint16_t camera_height;     /* 240 */
    uint8_t auto_stream;        /* 1: start DLP streaming on boot */
    uint32_t stream_interval_ms;/* minimum ms between DLP push frames (0=ASAP) */
    uint16_t target_track_id;   /* 0 = auto (closest to centre), >0 = specific ID */
    uint8_t target_class;       /* 0xFF = any class */
} dristy_pipeline_config_t;

#define DRISTY_PIPELINE_CONFIG_DEFAULT {    \
    .initial_mode       = DRISTY_MODE_DETECT_TRACK, \
    .camera_width       = 320,              \
    .camera_height      = 240,              \
    .auto_stream        = 0,                \
    .stream_interval_ms = 0,                \
    .target_track_id    = 0,                \
    .target_class       = 0xFF,             \
}

/* Pipeline state (read-only for external modules) */
typedef struct
{
    dristy_mode_t mode;
    uint32_t frame_count;
    uint32_t inference_count;
    uint32_t detection_count;
    uint32_t tag_count;
    uint32_t flow_count;
    uint32_t busy_drop_count;
    uint32_t last_inference_us;
    uint32_t last_pipeline_us;
    uint16_t fps_x10;
    uint8_t kpu_loaded;
    uint8_t kpu_busy;
    uint8_t running;
} dristy_pipeline_state_t;

/* Initialise the pipeline (call once at boot after system init) */
uint8_t dristy_pipeline_init(const dristy_pipeline_config_t *config);

/* Start the vision pipeline (begins camera capture and processing) */
uint8_t dristy_pipeline_start(void);

/* Stop the pipeline (halts camera and KPU) */
void dristy_pipeline_stop(void);

/* Tick — call from the main loop on core 0. Handles:
 * - KPU completion collection
 * - Post-processing dispatch to core 1
 * - Result bus commit
 * - FPS calculation
 * Returns 1 if a new result was committed this tick. */
uint8_t dristy_pipeline_tick(void);

/* Run classical (+ export) on an RGB565 preview frame already leased
 * by the LCD path. Avoids camera_stream_acquire_latest() stealing the
 * only READY slot from the display. */
uint8_t dristy_pipeline_ingest_rgb565(const volatile uint16_t *pixels,
                                      uint16_t width,
                                      uint16_t height);

/* Switch vision mode. Safe to call while running — the pipeline
 * will stop the current mode's resources and start the new one.
 * Returns 1 on success, 0 if mode is invalid. */
uint8_t dristy_pipeline_set_mode(dristy_mode_t mode);

/* Get current pipeline state */
const dristy_pipeline_state_t *dristy_pipeline_state(void);

/* Get the tracker (for DLP protocol to read tracks) */
dristy_tracker_t *dristy_pipeline_tracker(void);

/* Runtime adjustments */
void dristy_pipeline_set_target_track(uint16_t track_id);
void dristy_pipeline_set_target_class(uint8_t class_id);
void dristy_pipeline_set_confidence(uint8_t percent);
void dristy_pipeline_set_nms(uint8_t percent);

#endif /* DRISTY_PIPELINE_H */
