#include "dristy_pose.h"

#include <math.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Homography-based PnP for planar targets
 *
 * Given 4 image-world correspondences on a plane (Z=0), we compute:
 *   1. Homography H mapping world (X,Y) → normalised image (u,v)
 *   2. Decompose H into [r1 | r2 | t] via the DLT normal equations
 *   3. Enforce R ∈ SO(3) via Gram-Schmidt orthogonalisation
 *   4. Extract Euler angles from R
 *   5. Compute reprojection error for quality assessment
 *
 * This is a simplified but effective approach for square fiducial markers.
 * The full DLT would use SVD, but for 4 well-conditioned correspondences
 * on a square, the direct homography method is sufficient and fast.
 * ------------------------------------------------------------------------- */

static dristy_camera_intrinsics_t g_intrinsics = DRISTY_INTRINSICS_OV2640_320x240;

void dristy_pose_set_intrinsics(const dristy_camera_intrinsics_t *intrinsics)
{
    if(intrinsics)
        g_intrinsics = *intrinsics;
}

const dristy_camera_intrinsics_t *dristy_pose_get_intrinsics(void)
{
    return &g_intrinsics;
}

void dristy_undistort_point(const dristy_camera_intrinsics_t *intr,
                            float px, float py,
                            float *ux, float *uy)
{
    /* Normalise to camera coordinates */
    float x = (px - intr->cx) / intr->fx;
    float y = (py - intr->cy) / intr->fy;
    float r2 = x * x + y * y;
    float radial = 1.0f + intr->k1 * r2 + intr->k2 * r2 * r2;

    /* Undistort and re-project */
    *ux = x * radial * intr->fx + intr->cx;
    *uy = y * radial * intr->fy + intr->cy;
}

/* --- 3×3 Matrix Utilities ----------------------------------------------- */

static float vec3_dot(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static float vec3_norm(const float *v)
{
    return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

static void vec3_scale(float *v, float s)
{
    v[0] *= s; v[1] *= s; v[2] *= s;
}

static void vec3_cross(const float *a, const float *b, float *c)
{
    c[0] = a[1] * b[2] - a[2] * b[1];
    c[1] = a[2] * b[0] - a[0] * b[2];
    c[2] = a[0] * b[1] - a[1] * b[0];
}

/* Solve 8×9 DLT system for homography via simplified method:
 * For exactly 4 points, compute H directly from the point correspondences.
 * Uses the normalised Direct Linear Transform. */
static uint8_t compute_homography(const dristy_point2f_t img[4],
                                  const float world[4][2],
                                  float H[9])
{
    /* Build the 8×9 system A·h = 0 from 4 correspondences.
     * For a compact embedded solver, use the closed-form for 4 points:
     * We rearrange to Ah = b (8×8 system) by moving H[8]=1 to the RHS. */
    float A[8][8];
    float b[8];
    float pivot, factor;
    uint8_t piv_row;

    memset(A, 0, sizeof(A));
    for(uint8_t i = 0; i < 4; i++)
    {
        float X = world[i][0], Y = world[i][1];
        float u = img[i].x, v = img[i].y;
        uint8_t r0 = i * 2, r1 = i * 2 + 1;

        /* Row r0: X, Y, 1, 0, 0, 0, -u*X, -u*Y  → u */
        A[r0][0] = X;  A[r0][1] = Y;  A[r0][2] = 1.0f;
        A[r0][6] = -u * X; A[r0][7] = -u * Y;
        b[r0] = u;

        /* Row r1: 0, 0, 0, X, Y, 1, -v*X, -v*Y  → v */
        A[r1][3] = X;  A[r1][4] = Y;  A[r1][5] = 1.0f;
        A[r1][6] = -v * X; A[r1][7] = -v * Y;
        b[r1] = v;
    }

    /* Gaussian elimination with partial pivoting (8×8) */
    for(uint8_t col = 0; col < 8; col++)
    {
        /* Find pivot */
        float max_val = 0.0f;
        piv_row = col;
        for(uint8_t row = col; row < 8; row++)
        {
            float av = fabsf(A[row][col]);
            if(av > max_val) { max_val = av; piv_row = row; }
        }
        if(max_val < 1e-10f)
            return 0; /* singular matrix — degenerate geometry */

        /* Swap rows */
        if(piv_row != col)
        {
            for(uint8_t k = 0; k < 8; k++)
            {
                float tmp = A[col][k]; A[col][k] = A[piv_row][k]; A[piv_row][k] = tmp;
            }
            float tmp = b[col]; b[col] = b[piv_row]; b[piv_row] = tmp;
        }

        pivot = A[col][col];
        /* Eliminate below */
        for(uint8_t row = col + 1; row < 8; row++)
        {
            factor = A[row][col] / pivot;
            for(uint8_t k = col; k < 8; k++)
                A[row][k] -= factor * A[col][k];
            b[row] -= factor * b[col];
        }
    }

    /* Back substitution */
    float h[8];
    for(int8_t row = 7; row >= 0; row--)
    {
        float sum = b[row];
        for(uint8_t k = (uint8_t)(row + 1); k < 8; k++)
            sum -= A[row][k] * h[k];
        if(fabsf(A[row][row]) < 1e-10f)
            return 0;
        h[row] = sum / A[row][row];
    }

    /* H = [[h0,h1,h2],[h3,h4,h5],[h6,h7,1]] in row-major */
    for(uint8_t i = 0; i < 8; i++)
        H[i] = h[i];
    H[8] = 1.0f;
    return 1;
}

uint8_t dristy_pose_from_tag(const dristy_point2f_t corners[4],
                             float tag_size_mm,
                             const dristy_camera_intrinsics_t *intr,
                             dristy_pose_t *out)
{
    float half = tag_size_mm / 2.0f;
    float H[9];
    float r1[3], r2[3], r3[3], t[3];
    float norm1, norm2, scale;

    memset(out, 0, sizeof(*out));
    if(!intr || tag_size_mm <= 0.0f)
        return 0;

    /* World coordinates: tag corners in the tag's XY plane (Z=0)
     * Standard AprilTag corner ordering: bottom-left, bottom-right,
     * top-right, top-left (counter-clockwise from detector) */
    float world[4][2] = {
        {-half, -half},
        { half, -half},
        { half,  half},
        {-half,  half},
    };

    /* Undistort image corners */
    dristy_point2f_t undist[4];
    for(uint8_t i = 0; i < 4; i++)
    {
        dristy_undistort_point(intr,
                               corners[i].x, corners[i].y,
                               &undist[i].x, &undist[i].y);
    }

    /* Normalise to camera coordinates: p_norm = K⁻¹ · p_pixel */
    dristy_point2f_t norm[4];
    for(uint8_t i = 0; i < 4; i++)
    {
        norm[i].x = (undist[i].x - intr->cx) / intr->fx;
        norm[i].y = (undist[i].y - intr->cy) / intr->fy;
    }

    /* Compute homography in normalised camera coordinates */
    if(!compute_homography(norm, world, H))
        return 0;

    /* Decompose H = [r1 | r2 | t]
     * H maps (X,Y) → (u_norm, v_norm) where world Z=0.
     * Column vectors of H = λ·[r1 | r2 | t] */
    r1[0] = H[0]; r1[1] = H[3]; r1[2] = H[6];
    r2[0] = H[1]; r2[1] = H[4]; r2[2] = H[7];
    t[0]  = H[2]; t[1]  = H[5]; t[2]  = H[8];

    /* Normalise: scale factor λ = 1/||r1|| (or average of ||r1|| and ||r2||) */
    norm1 = vec3_norm(r1);
    norm2 = vec3_norm(r2);
    if(norm1 < 1e-8f || norm2 < 1e-8f)
        return 0;

    scale = 2.0f / (norm1 + norm2); /* average for stability */

    vec3_scale(r1, scale);
    vec3_scale(r2, scale);
    vec3_scale(t, scale);

    /* Enforce orthogonality: r3 = r1 × r2 */
    vec3_cross(r1, r2, r3);

    /* Gram-Schmidt on r1,r2 to ensure exact orthogonality:
     * r2' = r2 - (r2·r1)r1, then normalise both */
    float d = vec3_dot(r1, r2);
    r2[0] -= d * r1[0]; r2[1] -= d * r1[1]; r2[2] -= d * r1[2];

    float n1 = vec3_norm(r1);
    float n2 = vec3_norm(r2);
    if(n1 < 1e-8f || n2 < 1e-8f)
        return 0;
    vec3_scale(r1, 1.0f / n1);
    vec3_scale(r2, 1.0f / n2);
    vec3_cross(r1, r2, r3);

    /* Ensure proper rotation (det(R) = +1): if t_z < 0, flip sign */
    if(t[2] < 0.0f)
    {
        vec3_scale(r1, -1.0f);
        vec3_scale(r2, -1.0f);
        vec3_scale(r3, -1.0f);
        vec3_scale(t, -1.0f);
    }

    /* Store rotation matrix (column-major: R[:,0]=r1, R[:,1]=r2, R[:,2]=r3) */
    out->R[0] = r1[0]; out->R[1] = r1[1]; out->R[2] = r1[2];
    out->R[3] = r2[0]; out->R[4] = r2[1]; out->R[5] = r2[2];
    out->R[6] = r3[0]; out->R[7] = r3[1]; out->R[8] = r3[2];

    /* Translation */
    out->tx = t[0];
    out->ty = t[1];
    out->tz = t[2];

    /* Euler angles (ZYX convention) from rotation matrix
     * R = Rz(yaw) · Ry(pitch) · Rx(roll) */
    out->pitch = asinf(-out->R[2]); /* -r13 */
    float cp = cosf(out->pitch);
    if(fabsf(cp) > 1e-6f)
    {
        out->roll = atan2f(out->R[5], out->R[8]); /* r23/r33 */
        out->yaw  = atan2f(out->R[1], out->R[0]); /* r12/r11 */
    }
    else
    {
        /* Gimbal lock */
        out->roll = 0.0f;
        out->yaw  = atan2f(-out->R[3], out->R[4]);
    }

    /* Convert to degrees */
    out->roll  *= 180.0f / 3.14159265f;
    out->pitch *= 180.0f / 3.14159265f;
    out->yaw   *= 180.0f / 3.14159265f;

    /* Range: distance from camera to tag centre */
    out->range = sqrtf(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);

    /* Reprojection error: project world corners back and measure RMS pixel error */
    {
        float total_err = 0.0f;
        for(uint8_t i = 0; i < 4; i++)
        {
            float X = world[i][0], Y = world[i][1];
            /* p_cam = R·[X,Y,0]' + t */
            float px = r1[0]*X + r2[0]*Y + t[0];
            float py = r1[1]*X + r2[1]*Y + t[1];
            float pz = r1[2]*X + r2[2]*Y + t[2];

            if(fabsf(pz) < 1e-6f) { out->valid = 0; return 0; }

            /* Project to pixel */
            float u = (px / pz) * intr->fx + intr->cx;
            float v = (py / pz) * intr->fy + intr->cy;

            float dx = u - corners[i].x;
            float dy = v - corners[i].y;
            total_err += dx * dx + dy * dy;
        }
        out->reproj_error = sqrtf(total_err / 4.0f);
    }

    out->valid = 1;
    return 1;
}
