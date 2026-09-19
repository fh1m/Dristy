#ifndef DRISTY_RESULT_BUS_H
#define DRISTY_RESULT_BUS_H

#include <stdint.h>

#include "dristy_modes.h"
#include "dristy_tracker.h"
#include "dristy_pose.h"

/* ---------------------------------------------------------------------------
 * Dristy Result Bus — Control-System-Ready Vision Output
 *
 * Central aggregation point for ALL vision pipeline outputs. Every frame
 * produces exactly one result_bus snapshot. The host protocol, LCD renderer,
 * and any future ROS/MAVLink bridge all read from this bus.
 *
 * Design for drone/rover/AUV integration:
 *
 * 1. TIMESTAMPED: every snapshot has a monotonic µs timestamp from boot.
 *    When fusing with IMU, the host matches vision timestamps to IMU samples.
 *
 * 2. NORMALISED COORDINATES: all positions are in [-1000, +1000] range
 *    centered on the frame (0,0 = centre). This is directly usable as a
 *    PID error signal without knowing the camera resolution.
 *    norm_x = (pixel_x - width/2)  * 2000 / width
 *    norm_y = (pixel_y - height/2) * 2000 / height
 *
 * 3. CONTROL-PRIORITY OUTPUT: for landing/tracking modes, a single
 *    "primary target" struct is always at index 0 — the flight controller
 *    doesn't need to search through arrays.
 *
 * 4. HEARTBEAT: frame counter + mode + status. If frame_count stops
 *    incrementing, the flight controller knows vision is dead.
 *
 * 5. LATENCY TRACKING: pipeline_us measures total sensor→result time.
 *    The host can compensate for this delay in its control loop.
 *
 * Memory: double-buffered, ~2 KB per snapshot. Core 0 writes the "back"
 * buffer, atomically swaps to "front". Core 1 and DLP read from "front".
 * ------------------------------------------------------------------------- */

/* Maximum items per result type */
#define DRISTY_MAX_DETECTIONS   20
#define DRISTY_MAX_TRACKS       20
#define DRISTY_MAX_TAGS         8
#define DRISTY_MAX_QR           4
#define DRISTY_MAX_BLOBS        16
#define DRISTY_MAX_FLOW_CELLS   16  /* sparse flow grid */

/* Normalised coordinate: [-1000, +1000], centre = 0 */
typedef int16_t dristy_norm_t;

/* --- Detection result (from KPU + YOLO decode + NMS) -------------------- */
typedef struct
{
    /* Image coordinates (pixels, origin top-left) */
    int16_t x, y, w, h;
    /* Normalised centre for control ([-1000, +1000]) */
    dristy_norm_t norm_cx, norm_cy;
    /* Normalised size (0..2000, proportion of frame) */
    uint16_t norm_w, norm_h;
    /* Classification */
    uint8_t cls;
    uint16_t confidence;    /* ×1000 */
} dristy_result_detection_t;

/* --- Tracked object (from tracker, with velocity) ----------------------- */
typedef struct
{
    /* Pixel coordinates */
    int16_t cx, cy, w, h;
    /* Normalised for control */
    dristy_norm_t norm_cx, norm_cy;
    /* Velocity in normalised units per second (not per frame!) */
    int16_t vel_norm_x;     /* norm units / second */
    int16_t vel_norm_y;
    /* Identity */
    uint16_t id;
    uint8_t cls;
    uint16_t confidence;
    /* Status */
    uint8_t coasting;       /* 1 if track is coasting (predicted, not seen) */
    uint32_t age_frames;
    /* Time-to-contact: tz / vz for approaching objects (ms, 0 = unknown) */
    uint16_t ttc_ms;
} dristy_result_track_t;

/* --- AprilTag / fiducial with 6-DOF pose -------------------------------- */
typedef struct
{
    uint16_t tag_id;
    uint8_t family;         /* 0=TAG36H11, 1=TAG25H9, 2=TAG16H5 */
    uint8_t hamming;
    /* 2D in image */
    int16_t cx, cy;
    dristy_norm_t norm_cx, norm_cy;
    /* 3D pose (mm and degrees) — only valid if pose_valid=1 */
    uint8_t pose_valid;
    float tx_mm, ty_mm, tz_mm;
    float roll_deg, pitch_deg, yaw_deg;
    float range_mm;
    float reproj_error;
    /* Corner pixel coordinates for rendering */
    int16_t corners[4][2];
} dristy_result_tag_t;

/* --- QR code ------------------------------------------------------------ */
typedef struct
{
    uint16_t data_len;
    char data[128];         /* decoded UTF-8 string */
    int16_t corners[4][2];
    dristy_norm_t norm_cx, norm_cy;
} dristy_result_qr_t;

/* --- Colour blob -------------------------------------------------------- */
typedef struct
{
    int16_t cx, cy, w, h;
    dristy_norm_t norm_cx, norm_cy;
    uint32_t pixel_count;
    uint8_t colour_id;      /* user-assigned colour slot */
    float density;          /* pixels / bbox area, 0..1 */
    float roundness;        /* 4π·area/perimeter², 0..1 (1=circle) */
} dristy_result_blob_t;

/* --- Optical flow cell -------------------------------------------------- */
typedef struct
{
    /* ROI centre in normalised coords */
    dristy_norm_t roi_cx, roi_cy;
    /* Displacement in sub-pixel units × 100 */
    int16_t dx_x100, dy_x100;
    /* Confidence (0..1000) */
    uint16_t response;
} dristy_result_flow_t;

/* --- Line regression ---------------------------------------------------- */
typedef struct
{
    /* Line endpoints in normalised coords */
    dristy_norm_t x1, y1, x2, y2;
    /* Line angle in degrees × 10 (0 = horizontal, 900 = vertical) */
    int16_t angle_x10;
    /* Offset from frame centre in normalised units (for line-following PID) */
    dristy_norm_t offset;
    /* Curvature estimate (0 = straight) */
    int16_t curvature_x100;
} dristy_result_line_t;

/* --- Primary target for control loops ----------------------------------- */
typedef enum
{
    DRISTY_TARGET_NONE = 0,
    DRISTY_TARGET_DETECTION,
    DRISTY_TARGET_TRACK,
    DRISTY_TARGET_TAG,
    DRISTY_TARGET_BLOB,
    DRISTY_TARGET_LINE,
} dristy_target_type_t;

typedef struct
{
    dristy_target_type_t type;

    /* Normalised error from frame centre — DIRECTLY usable as PID setpoint error
     * ex: if target is at norm_cx=+200, the drone should yaw right to bring it to 0.
     * Range: [-1000, +1000]. 0 = centred. */
    dristy_norm_t error_x;
    dristy_norm_t error_y;

    /* Normalised size — useful for distance/approach control
     * Larger size = closer. Range: [0, 2000]. */
    uint16_t size;

    /* Velocity error rate (for derivative term in PID) */
    int16_t error_rate_x;   /* d(error_x)/dt in norm units/second */
    int16_t error_rate_y;

    /* For fiducial targets: 3D pose (if available) */
    uint8_t has_3d;
    float range_mm;         /* distance to target */
    float bearing_deg;      /* horizontal angle to target */
    float elevation_deg;    /* vertical angle to target */

    /* Track ID (for tracked targets) */
    uint16_t track_id;
    uint8_t cls;
    uint16_t confidence;
} dristy_primary_target_t;

/* --- The full result bus snapshot --------------------------------------- */
typedef struct
{
    /* Timing */
    uint64_t timestamp_us;      /* monotonic µs from boot */
    uint32_t frame_number;      /* monotonic frame counter */
    uint32_t pipeline_us;       /* total sensor→result latency */
    uint32_t inference_us;      /* KPU inference time (0 if no KPU) */

    /* System state */
    dristy_mode_t mode;
    uint8_t kpu_active;
    uint16_t fps_x10;           /* measured FPS × 10 */

    /* PRIMARY TARGET — index 0 for the control loop */
    dristy_primary_target_t target;

    /* Detections (from KPU + NMS, before tracking) */
    uint8_t detection_count;
    dristy_result_detection_t detections[DRISTY_MAX_DETECTIONS];

    /* Tracked objects (from tracker, with velocity and ID) */
    uint8_t track_count;
    dristy_result_track_t tracks[DRISTY_MAX_TRACKS];

    /* Fiducial tags (AprilTag, with optional 6-DOF pose) */
    uint8_t tag_count;
    dristy_result_tag_t tags[DRISTY_MAX_TAGS];

    /* QR codes */
    uint8_t qr_count;
    dristy_result_qr_t qr[DRISTY_MAX_QR];

    /* Colour blobs */
    uint8_t blob_count;
    dristy_result_blob_t blobs[DRISTY_MAX_BLOBS];

    /* Optical flow */
    uint8_t flow_count;
    dristy_result_flow_t flow[DRISTY_MAX_FLOW_CELLS];

    /* Line detection */
    uint8_t line_valid;
    dristy_result_line_t line;

} dristy_result_snapshot_t;

/* --- API ---------------------------------------------------------------- */

/* Initialise the double-buffered result bus */
void dristy_result_bus_init(void);

/* Begin writing a new snapshot (returns pointer to back buffer) */
dristy_result_snapshot_t *dristy_result_bus_begin_write(void);

/* Commit the back buffer → front (atomic swap). Call after filling. */
void dristy_result_bus_commit(void);

/* Read the latest committed snapshot (front buffer). Thread-safe.
 * Returns NULL if no snapshot has been committed yet. */
const dristy_result_snapshot_t *dristy_result_bus_read(void);

/* Get the frame number of the latest committed snapshot.
 * Returns 0 if nothing committed. Useful for polling without full read. */
uint32_t dristy_result_bus_frame_number(void);

/* --- Coordinate conversion helpers -------------------------------------- */

/* Convert pixel coordinate to normalised [-1000, +1000] */
static inline dristy_norm_t dristy_pixel_to_norm_x(int16_t px, uint16_t width)
{
    return (dristy_norm_t)(((int32_t)px - (int32_t)width / 2) * 2000 / (int32_t)width);
}

static inline dristy_norm_t dristy_pixel_to_norm_y(int16_t py, uint16_t height)
{
    return (dristy_norm_t)(((int32_t)py - (int32_t)height / 2) * 2000 / (int32_t)height);
}

/* Convert normalised back to pixel (for rendering) */
static inline int16_t dristy_norm_to_pixel_x(dristy_norm_t n, uint16_t width)
{
    return (int16_t)((int32_t)n * (int32_t)width / 2000 + (int32_t)width / 2);
}

static inline int16_t dristy_norm_to_pixel_y(dristy_norm_t n, uint16_t height)
{
    return (int16_t)((int32_t)n * (int32_t)height / 2000 + (int32_t)height / 2);
}

#endif /* DRISTY_RESULT_BUS_H */
