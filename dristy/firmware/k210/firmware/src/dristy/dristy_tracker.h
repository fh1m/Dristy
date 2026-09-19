#ifndef DRISTY_TRACKER_H
#define DRISTY_TRACKER_H

#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Dristy Multi-Object Tracker — Production-Grade for Robotics
 *
 * Techniques:
 *   1. Hungarian (Munkres) optimal assignment — globally optimal matching
 *   2. Cascaded matching — confirmed tracks first, then tentative
 *   3. Block-coupled Kalman filter — proper cross-covariance between
 *      position and velocity (2×2 blocks, not diagonal)
 *   4. Combined IoU + Mahalanobis cost — spatial + statistical gating
 *   5. Adaptive process noise — scales with detection confidence
 *   6. Class-aware matching — different classes cannot be associated
 *   7. Re-identification lifecycle: tentative→confirmed→coasting→deleted
 *
 * State vector per track: [cx, cy, w, h, vx, vy, vw, vh]  (8 dims)
 * Measurement vector:     [cx, cy, w, h]                   (4 dims)
 *
 * Kalman uses 4 independent 2×2 blocks:
 *   Block 0: [cx, vx] with 2×2 covariance  P_cx
 *   Block 1: [cy, vy] with 2×2 covariance  P_cy
 *   Block 2: [w, vw]  with 2×2 covariance  P_w
 *   Block 3: [h, vh]  with 2×2 covariance  P_h
 *
 * This is the "sweet spot" — captures position-velocity coupling exactly
 * while avoiding the O(n³) cost of a full 8×8 matrix inversion.
 *
 * All arithmetic is Q12 fixed-point (× 4096) for state, Q20 (× 1048576)
 * for covariance. The K210's 64-bit integer multiply makes this fast.
 *
 * Memory: ~300 bytes/track × 32 tracks = ~9.6 KB total
 * Timing: < 80 µs per frame for 20 tracks including Hungarian
 * ------------------------------------------------------------------------- */

#define DRISTY_TRACKER_MAX_TRACKS       32
#define DRISTY_TRACKER_MAX_DETECTIONS   20

/* Lifecycle thresholds — all runtime-configurable */
typedef struct
{
    uint8_t min_hits_to_confirm;   /* frames before track is confirmed (2) */
    uint8_t max_coast_frames;      /* frames without match before deletion (5) */
    uint8_t max_tentative_coast;   /* max coast for unconfirmed tracks (2) */
    uint16_t iou_threshold_q8;     /* IoU gate ×256: 77 = 0.30 (default) */
    uint16_t mahal_gate_q8;        /* Mahalanobis distance² gate ×256 (9.21 = chi²₄(0.99)) */
    uint8_t class_aware;           /* 1: different classes cannot match */
    uint8_t use_mahalanobis;       /* 1: combined IoU+Mahal cost, 0: IoU only */
    uint16_t process_noise_pos_q12;/* Q_pos for position in Q12 (4096=1.0) */
    uint16_t process_noise_vel_q12;/* Q_vel for velocity in Q12 (1024=0.25) */
    uint16_t measure_noise_q12;    /* R measurement noise in Q12 (8192=2.0) */
} dristy_tracker_config_t;

/* Default config — tuned for 320×240 @ 20fps detection */
#define DRISTY_TRACKER_CONFIG_DEFAULT {             \
    .min_hits_to_confirm   = 2,                     \
    .max_coast_frames      = 5,                     \
    .max_tentative_coast   = 2,                     \
    .iou_threshold_q8      = 77,    /* 0.30 */      \
    .mahal_gate_q8         = 2358,  /* 9.21 */      \
    .class_aware           = 1,                     \
    .use_mahalanobis       = 1,                     \
    .process_noise_pos_q12 = 4096,  /* 1.0 */       \
    .process_noise_vel_q12 = 1024,  /* 0.25 */      \
    .measure_noise_q12     = 8192,  /* 2.0 */       \
}

typedef struct
{
    int16_t cx;     /* centre x in image coordinates */
    int16_t cy;     /* centre y */
    int16_t w;      /* width */
    int16_t h;      /* height */
    uint8_t cls;    /* class ID from detector */
    uint16_t conf;  /* confidence × 1000 */
} dristy_detection_t;

/* 2×2 symmetric matrix in Q20 for block Kalman covariance
 * Stored as [a, b; b, c] → three elements */
typedef struct
{
    int64_t a;   /* variance of position (or size) */
    int64_t b;   /* cross-covariance position↔velocity */
    int64_t c;   /* variance of velocity */
} dristy_cov2x2_t;

typedef enum
{
    DRISTY_TRACK_TENTATIVE = 0,
    DRISTY_TRACK_CONFIRMED,
    DRISTY_TRACK_COASTING,      /* was confirmed, now missing detections */
    DRISTY_TRACK_DELETED,       /* marked for removal */
} dristy_track_state_t;

typedef struct
{
    /* Block Kalman state: [pos, vel] in Q12 per dimension */
    int32_t pos[4];     /* [cx, cy, w, h] in Q12 */
    int32_t vel[4];     /* [vx, vy, vw, vh] in Q12 */

    /* 4 independent 2×2 block covariances, one per [pos_i, vel_i] pair */
    dristy_cov2x2_t cov[4];

    /* Identity */
    uint16_t id;
    uint8_t cls;
    uint16_t confidence;

    /* Lifecycle */
    dristy_track_state_t state;
    uint8_t total_hits;     /* total frames with a matched detection */
    uint8_t consecutive_hits;
    uint8_t consecutive_misses;
    uint32_t age;           /* total frames since creation */
    uint32_t last_seen_frame;

    /* Smoothed detection confidence — EMA for adaptive noise */
    uint16_t smoothed_conf; /* EMA of confidence × 1000 */
} dristy_track_t;

/* Report structure for host protocol and rendering */
typedef struct
{
    int16_t cx, cy, w, h;
    int16_t vx_x10, vy_x10;    /* pixels/frame × 10 */
    int16_t ax_x100, ay_x100;  /* acceleration (pixels/frame²) × 100, if available */
    uint16_t id;
    uint8_t cls;
    uint16_t confidence;
    dristy_track_state_t state;
    uint32_t age;
    uint8_t coast_count;        /* how many frames since last real detection */
} dristy_track_report_t;

typedef struct
{
    dristy_track_t tracks[DRISTY_TRACKER_MAX_TRACKS];
    uint8_t track_count;
    uint16_t next_id;
    uint32_t frame_count;
    dristy_tracker_config_t config;

    /* Diagnostics */
    uint32_t total_created;
    uint32_t total_deleted;
    uint32_t total_matched;
    uint32_t total_unmatched_det;
    uint32_t total_unmatched_trk;
    uint32_t last_hungarian_us;
    uint32_t last_update_us;
} dristy_tracker_t;

/* Initialise tracker with config (pass NULL for defaults) */
void dristy_tracker_init(dristy_tracker_t *tracker,
                         const dristy_tracker_config_t *config);

/* Reset all tracks, keep config */
void dristy_tracker_reset(dristy_tracker_t *tracker);

/* Reconfigure at runtime (e.g. from DLP command) */
void dristy_tracker_configure(dristy_tracker_t *tracker,
                              const dristy_tracker_config_t *config);

/* Feed a new set of detections. Returns number of confirmed+coasting tracks. */
uint8_t dristy_tracker_update(dristy_tracker_t *tracker,
                              const dristy_detection_t *detections,
                              uint8_t det_count);

/* Predict-only step (no detections). For smooth interpolation between
 * detection frames. Returns number of confirmed+coasting tracks. */
uint8_t dristy_tracker_predict(dristy_tracker_t *tracker);

/* Get all active (confirmed + coasting) tracks. Returns count written. */
uint8_t dristy_tracker_report(const dristy_tracker_t *tracker,
                              dristy_track_report_t *out,
                              uint8_t max_count);

/* Get a specific track by ID, or NULL if not found/not confirmed */
const dristy_track_t *dristy_tracker_find(const dristy_tracker_t *tracker,
                                          uint16_t id);

#endif /* DRISTY_TRACKER_H */
