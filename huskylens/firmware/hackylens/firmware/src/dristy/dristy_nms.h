#ifndef DRISTY_NMS_H
#define DRISTY_NMS_H

#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Dristy Non-Maximum Suppression
 *
 * Three methods, all configurable at runtime:
 *
 * 1. Hard NMS (classic): IoU > threshold → suppress entirely
 *    + Fast, predictable
 *    - Drops valid overlapping detections
 *
 * 2. Soft-NMS (Gaussian): IoU > 0 → decay confidence by exp(-IoU²/σ)
 *    + Preserves partially overlapping objects (e.g., people in a crowd)
 *    + Strictly better mAP than hard NMS in benchmarks
 *    - Slightly more output detections to process
 *
 * 3. Soft-NMS (Linear): IoU > threshold → confidence *= (1 - IoU)
 *    + Cheaper than Gaussian, still better than hard NMS
 *    - Less smooth decay curve
 *
 * For robotics, Soft-NMS (Gaussian) is the default. When objects are
 * well-separated (e.g., ArUco markers), hard NMS is fine and faster.
 * ------------------------------------------------------------------------- */

typedef enum
{
    DRISTY_NMS_HARD     = 0,
    DRISTY_NMS_SOFT_GAUSSIAN,
    DRISTY_NMS_SOFT_LINEAR,
} dristy_nms_method_t;

typedef struct
{
    int16_t x, y, w, h;
    uint8_t cls;
    float score;
} dristy_nms_box_t;

typedef struct
{
    dristy_nms_method_t method;
    float iou_threshold;        /* for hard NMS and linear soft-NMS */
    float sigma;                /* for Gaussian soft-NMS (default 0.5) */
    float score_threshold;      /* discard boxes below this confidence */
    uint8_t class_agnostic;     /* 1: NMS across all classes, 0: per-class */
} dristy_nms_config_t;

#define DRISTY_NMS_CONFIG_DEFAULT {                 \
    .method          = DRISTY_NMS_SOFT_GAUSSIAN,    \
    .iou_threshold   = 0.45f,                       \
    .sigma           = 0.5f,                        \
    .score_threshold = 0.15f,                       \
    .class_agnostic  = 0,                           \
}

/* In-place NMS. Suppressed boxes have score set to 0.
 * Returns number of surviving boxes (score > 0).
 * boxes array is sorted by descending score on output. */
uint8_t dristy_nms_apply(dristy_nms_box_t *boxes, uint8_t count,
                         const dristy_nms_config_t *config);

#endif /* DRISTY_NMS_H */
