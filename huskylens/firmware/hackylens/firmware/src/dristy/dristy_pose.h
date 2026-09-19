#ifndef DRISTY_POSE_H
#define DRISTY_POSE_H

#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Dristy PnP Pose Estimator
 *
 * Computes 6-DOF camera-to-tag pose from 4 corner correspondences.
 * Uses the Iterative Closest Point / DLT + refinement approach optimised
 * for the specific case of a known-size planar tag.
 *
 * For a square fiducial marker with known side length, the 4 corners in 3D
 * world coordinates are fixed (e.g., ±size/2 in X and Y, Z=0). Given their
 * pixel coordinates, we solve for R and t.
 *
 * Uses the homography → decomposition method:
 *   1. Compute homography H from 4 point correspondences
 *   2. Decompose H = K⁻¹ · [r1 | r2 | t] into rotation + translation
 *   3. Enforce orthogonality of R via SVD approximation
 *
 * Camera intrinsics must be calibrated once and stored. Default values are
 * provided for OV2640 at 320×240 (approximate).
 * ------------------------------------------------------------------------- */

/* Camera intrinsic parameters */
typedef struct
{
    float fx;       /* focal length in pixels, X */
    float fy;       /* focal length in pixels, Y */
    float cx;       /* principal point X */
    float cy;       /* principal point Y */
    float k1;       /* radial distortion coefficient 1 (barrel) */
    float k2;       /* radial distortion coefficient 2 */
} dristy_camera_intrinsics_t;

/* Default intrinsics for OV2640 @ 320×240 (approximate, needs calibration) */
#define DRISTY_INTRINSICS_OV2640_320x240 {  \
    .fx = 225.0f,                            \
    .fy = 225.0f,                            \
    .cx = 160.0f,                            \
    .cy = 120.0f,                            \
    .k1 = 0.0f,                              \
    .k2 = 0.0f,                              \
}

/* 6-DOF pose result */
typedef struct
{
    /* Translation: camera-to-tag in tag's coordinate frame (mm) */
    float tx;
    float ty;
    float tz;

    /* Rotation: Euler angles in degrees (ZYX convention) */
    float roll;     /* around X axis */
    float pitch;    /* around Y axis */
    float yaw;      /* around Z axis */

    /* Rotation matrix (column-major, 3×3) */
    float R[9];

    /* Quality metric: reprojection error in pixels (lower = better) */
    float reproj_error;

    /* Range: Euclidean distance from camera to tag centre (mm) */
    float range;

    /* Valid flag: 0 if pose could not be computed */
    uint8_t valid;
} dristy_pose_t;

/* 2D image point (sub-pixel) */
typedef struct
{
    float x;
    float y;
} dristy_point2f_t;

/* Compute 6-DOF pose from 4 tag corners.
 * corners[0..3] are in image coordinates (pixels).
 * tag_size_mm is the outer edge-to-edge size of the tag in millimetres.
 * Returns 1 on success, 0 on degenerate geometry. */
uint8_t dristy_pose_from_tag(const dristy_point2f_t corners[4],
                             float tag_size_mm,
                             const dristy_camera_intrinsics_t *intrinsics,
                             dristy_pose_t *out);

/* Undistort a single 2D point using the camera's radial distortion model.
 * Useful for pre-processing corners before pose estimation. */
void dristy_undistort_point(const dristy_camera_intrinsics_t *intrinsics,
                            float px, float py,
                            float *ux, float *uy);

/* Set/get global camera calibration (stored in settings journal on flash) */
void dristy_pose_set_intrinsics(const dristy_camera_intrinsics_t *intrinsics);
const dristy_camera_intrinsics_t *dristy_pose_get_intrinsics(void);

#endif /* DRISTY_POSE_H */
