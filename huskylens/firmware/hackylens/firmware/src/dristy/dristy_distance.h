#ifndef DRISTY_DISTANCE_H
#define DRISTY_DISTANCE_H

#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Dristy Monocular Distance Estimation
 *
 * Estimates distance to objects using a single camera (no stereo/depth sensor).
 *
 * Methods:
 *
 * 1. KNOWN-SIZE: If real-world size of the object is known (e.g., a drone
 *    landing pad is 300mm), distance = (focal_length × real_size) / pixel_size.
 *    Accurate to ~5% at close range, degrades with distance.
 *
 * 2. KNOWN-HEIGHT (ground plane): If the camera height and pitch are known,
 *    and the object is on the ground, use geometry:
 *    distance = camera_height / tan(pitch + pixel_angle)
 *    Useful for ground robots.
 *
 * 3. CROSS-FRAME VELOCITY: If the object is approaching at known speed,
 *    measure apparent size growth rate → estimate range.
 *    Used in TTC (time-to-contact) calculations.
 *
 * 4. FIDUCIAL (AprilTag/ArUco): Uses PnP pose from dristy_pose.h.
 *    Most accurate method. Returns exact 3D position.
 *
 * All methods feed their results into the result bus range_mm field.
 * The flight controller receives distance estimates in the TARGET packet.
 * ------------------------------------------------------------------------- */

/* Distance estimation method */
typedef enum
{
    DRISTY_DIST_KNOWN_SIZE      = 0,    /* known physical size */
    DRISTY_DIST_GROUND_PLANE    = 1,    /* camera on ground robot */
    DRISTY_DIST_TTC             = 2,    /* time-to-contact from size change */
    DRISTY_DIST_FIDUCIAL        = 3,    /* from PnP (handled by pose module) */
} dristy_dist_method_t;

/* Known-size reference for a class or track */
typedef struct
{
    uint8_t class_id;           /* 0xFF = default for all classes */
    float real_width_mm;        /* known real-world width */
    float real_height_mm;       /* known real-world height (0 = use width) */
} dristy_size_reference_t;

#define DRISTY_MAX_SIZE_REFS 16

/* Distance estimation config */
typedef struct
{
    dristy_dist_method_t method;

    /* Camera parameters */
    float focal_length_px;      /* focal length in pixels (from calibration) */

    /* Ground-plane parameters */
    float camera_height_mm;     /* camera mount height above ground */
    float camera_pitch_deg;     /* camera pitch angle (0=horizon, -90=straight down) */

    /* Known-size references */
    uint8_t num_size_refs;
    dristy_size_reference_t size_refs[DRISTY_MAX_SIZE_REFS];
} dristy_distance_config_t;

#define DRISTY_DISTANCE_CONFIG_DEFAULT {    \
    .method           = DRISTY_DIST_KNOWN_SIZE, \
    .focal_length_px  = 230.0f,             \
    .camera_height_mm = 0.0f,               \
    .camera_pitch_deg = 0.0f,               \
    .num_size_refs    = 0,                  \
}

/* --- API ---------------------------------------------------------------- */

/* Initialise distance estimator */
void dristy_distance_init(const dristy_distance_config_t *config);

/* Add a size reference for a class (e.g., "person" ≈ 500mm wide) */
void dristy_distance_add_size_ref(uint8_t class_id,
                                  float real_width_mm,
                                  float real_height_mm);

/* Estimate distance to an object given its bounding box size in pixels.
 * Returns distance in mm, or 0 if cannot estimate. */
float dristy_distance_from_bbox(uint8_t class_id,
                                uint16_t bbox_w, uint16_t bbox_h);

/* Estimate distance from ground plane geometry.
 * pixel_y: vertical pixel position of object bottom edge. */
float dristy_distance_from_ground(uint16_t pixel_y, uint16_t frame_height);

/* Estimate TTC from two consecutive apparent sizes.
 * Returns time-to-contact in ms (0 = not approaching, UINT16_MAX = receding) */
uint16_t dristy_distance_ttc(uint16_t size_prev, uint16_t size_curr,
                             uint32_t dt_ms);

#endif /* DRISTY_DISTANCE_H */
