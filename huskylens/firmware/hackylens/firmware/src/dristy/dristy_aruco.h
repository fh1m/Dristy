#ifndef DRISTY_ARUCO_H
#define DRISTY_ARUCO_H

#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Dristy ArUco Marker Detector
 *
 * Detects ArUco markers commonly used in robotics (ROS, OpenCV, ArduPilot).
 * Pure CPU implementation, no KPU needed.
 *
 * Supports:
 *   - DICT_4X4_50:   4×4 bit grid, 50 markers   (ArUco original)
 *   - DICT_4X4_250:  4×4 bit grid, 250 markers
 *   - DICT_5X5_250:  5×5 bit grid, 250 markers
 *   - DICT_6X6_250:  6×6 bit grid, 250 markers
 *   - DICT_ARUCO_ORIGINAL: 5×5 bit grid, 1024 markers (OpenCV default)
 *
 * Detection algorithm:
 *   1. Adaptive threshold (Gaussian, block_size=7)
 *   2. Find contours → filter for quadrilaterals
 *   3. Perspective-unwarp each candidate quad
 *   4. Sample bit grid → decode marker ID
 *   5. Validate against dictionary (Hamming distance check)
 *   6. Refine corners to sub-pixel (optional)
 *   7. Compute 6-DOF pose via PnP (if marker size known)
 *
 * Key difference from AprilTag:
 *   - ArUco uses a simple binary grid with black border
 *   - No complex encoding; just raw bits + dictionary lookup
 *   - Faster to decode but less error-correcting than AprilTag
 *   - More commonly used with OpenCV / ROS ecosystem
 *
 * Memory: ~4 KB for detector state + dictionary tables
 * Speed: ~8-15 ms per frame at 320×240 (similar to AprilTag)
 * ------------------------------------------------------------------------- */

#define DRISTY_ARUCO_MAX_DETECTIONS  16
#define DRISTY_ARUCO_MAX_CANDIDATES  64

/* Supported dictionaries */
typedef enum
{
    DRISTY_ARUCO_DICT_4X4_50    = 0,
    DRISTY_ARUCO_DICT_4X4_250   = 1,
    DRISTY_ARUCO_DICT_5X5_250   = 2,
    DRISTY_ARUCO_DICT_6X6_250   = 3,
    DRISTY_ARUCO_DICT_ORIGINAL  = 4,  /* 5×5, 1024 markers */
    DRISTY_ARUCO_DICT_COUNT,
} dristy_aruco_dict_t;

/* Dictionary descriptor */
typedef struct
{
    uint8_t grid_size;          /* 4, 5, or 6 */
    uint16_t num_markers;       /* number of valid IDs */
    uint8_t max_hamming;        /* max Hamming distance for valid match */
    const uint8_t *data;        /* packed bit patterns, grid_size² bits per marker */
    uint16_t bytes_per_marker;  /* ceil(grid_size² / 8) */
} dristy_aruco_dict_info_t;

/* Single ArUco detection */
typedef struct
{
    uint16_t id;                /* marker ID */
    dristy_aruco_dict_t dict;   /* which dictionary matched */
    uint8_t hamming;            /* Hamming distance (0 = perfect match) */
    uint8_t rotation;           /* 0-3: how many 90° CW rotations applied */

    /* Corner coordinates (pixel, sub-pixel if refined) */
    float corners[4][2];        /* [top-left, top-right, bottom-right, bottom-left] */

    /* Centre */
    int16_t cx, cy;

    /* Bounding box */
    int16_t x, y, w, h;

    /* 6-DOF pose (only if marker_size_mm was set) */
    uint8_t pose_valid;
    float tx_mm, ty_mm, tz_mm;
    float roll_deg, pitch_deg, yaw_deg;
    float range_mm;
    float reproj_error;
} dristy_aruco_detection_t;

/* Detector configuration */
typedef struct
{
    dristy_aruco_dict_t dict;       /* which dictionary to search */
    uint8_t adaptive_thresh_c;      /* adaptive threshold constant (default: 7) */
    uint8_t adaptive_thresh_block;  /* block size (default: 7, must be odd) */
    float min_marker_perimeter;     /* minimum perimeter in pixels (default: 40) */
    float max_marker_perimeter;     /* maximum perimeter in pixels (default: 1200) */
    float polygonal_approx_accuracy;/* contour approx epsilon (default: 0.03) */
    float min_corner_distance;      /* minimum distance between corners (default: 5) */
    uint8_t max_hamming_distance;   /* max bit errors to accept (default: 1) */
    uint8_t refine_corners;         /* 1: sub-pixel corner refinement */
    float marker_size_mm;           /* physical size for pose estimation (0=skip) */
} dristy_aruco_config_t;

#define DRISTY_ARUCO_CONFIG_DEFAULT {   \
    .dict                     = DRISTY_ARUCO_DICT_4X4_50, \
    .adaptive_thresh_c        = 7,      \
    .adaptive_thresh_block    = 7,      \
    .min_marker_perimeter     = 40.0f,  \
    .max_marker_perimeter     = 1200.0f,\
    .polygonal_approx_accuracy= 0.03f,  \
    .min_corner_distance      = 5.0f,   \
    .max_hamming_distance     = 1,      \
    .refine_corners           = 1,      \
    .marker_size_mm           = 0.0f,   \
}

/* --- API ---------------------------------------------------------------- */

/* Initialise detector with given config. Returns 1 on success. */
uint8_t dristy_aruco_init(const dristy_aruco_config_t *config);

/* Reconfigure dictionary at runtime */
void dristy_aruco_set_dict(dristy_aruco_dict_t dict);

/* Set marker physical size for pose estimation (mm) */
void dristy_aruco_set_marker_size(float size_mm);

/* Detect ArUco markers in a grayscale image.
 * gray:   pointer to W×H uint8_t grayscale image
 * width:  image width
 * height: image height
 * Returns number of detections (0..DRISTY_ARUCO_MAX_DETECTIONS) */
uint8_t dristy_aruco_detect(const uint8_t *gray,
                            uint16_t width, uint16_t height);

/* Get detection results (valid after dristy_aruco_detect) */
const dristy_aruco_detection_t *dristy_aruco_results(uint8_t *count);

/* Get dictionary info */
const dristy_aruco_dict_info_t *dristy_aruco_dict_info(dristy_aruco_dict_t dict);

#endif /* DRISTY_ARUCO_H */
