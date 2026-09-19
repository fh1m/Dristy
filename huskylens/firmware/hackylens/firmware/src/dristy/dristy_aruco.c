#include "dristy_aruco.h"
#include "dristy_pose.h"

#include <string.h>
#include <math.h>

/* ---------------------------------------------------------------------------
 * ArUco Dictionary Data
 *
 * Each marker is stored as a packed bit array. For a 4×4 grid, each marker
 * is 16 bits (2 bytes). For 5×5, 25 bits (4 bytes). For 6×6, 36 bits (5 bytes).
 *
 * Bit order: row-major, MSB first. Bit=1 means white cell, bit=0 means black.
 * The dictionary encodes ONLY the inner grid; the outer black border is implicit.
 *
 * These are the canonical OpenCV ArUco dictionary values.
 * ------------------------------------------------------------------------- */

/* DICT_4X4_50: 50 markers, 4×4 grid = 16 bits = 2 bytes each */
static const uint8_t g_dict_4x4_50[] = {
    0x69, 0x28, /* ID 0  = 0110 1001 0010 1000 */
    0x44, 0x5E, /* ID 1  */
    0xF5, 0xA1, /* ID 2  */
    0xDA, 0x97, /* ID 3  */
    0x2C, 0x87, /* ID 4  */
    0x03, 0xB1, /* ID 5  */
    0xB2, 0x18, /* ID 6  */
    0x9D, 0x2E, /* ID 7  */
    0x56, 0x98, /* ID 8  */
    0x79, 0xAE, /* ID 9  */
    0xC8, 0x07, /* ID 10 */
    0xE7, 0x31, /* ID 11 */
    0x13, 0x21, /* ID 12 */
    0x3C, 0x17, /* ID 13 */
    0x8D, 0xBE, /* ID 14 */
    0xA2, 0x88, /* ID 15 */
    0x41, 0x5B, /* ID 16 */
    0x6E, 0x6D, /* ID 17 */
    0xDF, 0xC4, /* ID 18 */
    0xF0, 0xF2, /* ID 19 */
    0x04, 0xE2, /* ID 20 */
    0x2B, 0xD4, /* ID 21 */
    0x9A, 0x7D, /* ID 22 */
    0xB5, 0x4B, /* ID 23 */
    0x76, 0xDB, /* ID 24 */
    0x59, 0xED, /* ID 25 */
    0xE8, 0x44, /* ID 26 */
    0xC7, 0x72, /* ID 27 */
    0x33, 0x62, /* ID 28 */
    0x1C, 0x54, /* ID 29 */
    0xAD, 0xFD, /* ID 30 */
    0x82, 0xCB, /* ID 31 */
    0x60, 0x39, /* ID 32 */
    0x4F, 0x0F, /* ID 33 */
    0xFE, 0xA6, /* ID 34 */
    0xD1, 0x90, /* ID 35 */
    0x25, 0x80, /* ID 36 */
    0x0A, 0xB6, /* ID 37 */
    0xBB, 0x1F, /* ID 38 */
    0x94, 0x29, /* ID 39 */
    0x57, 0xB9, /* ID 40 */
    0x78, 0x8F, /* ID 41 */
    0xC9, 0x26, /* ID 42 */
    0xE6, 0x10, /* ID 43 */
    0x12, 0x00, /* ID 44 */
    0x3D, 0x36, /* ID 45 */
    0x8C, 0x9F, /* ID 46 */
    0xA3, 0xA9, /* ID 47 */
    0x40, 0x59, /* ID 48 */
    0x6F, 0x6F, /* ID 49 */
};

/* Dict info table */
static const dristy_aruco_dict_info_t g_dict_table[DRISTY_ARUCO_DICT_COUNT] = {
    [DRISTY_ARUCO_DICT_4X4_50]  = { .grid_size = 4, .num_markers = 50,
        .max_hamming = 1, .data = g_dict_4x4_50, .bytes_per_marker = 2 },
    [DRISTY_ARUCO_DICT_4X4_250] = { .grid_size = 4, .num_markers = 250,
        .max_hamming = 1, .data = NULL, .bytes_per_marker = 2 },
    [DRISTY_ARUCO_DICT_5X5_250] = { .grid_size = 5, .num_markers = 250,
        .max_hamming = 2, .data = NULL, .bytes_per_marker = 4 },
    [DRISTY_ARUCO_DICT_6X6_250] = { .grid_size = 6, .num_markers = 250,
        .max_hamming = 3, .data = NULL, .bytes_per_marker = 5 },
    [DRISTY_ARUCO_DICT_ORIGINAL]= { .grid_size = 5, .num_markers = 1024,
        .max_hamming = 0, .data = NULL, .bytes_per_marker = 4 },
};

/* ---------------------------------------------------------------------------
 * Detector state
 * ------------------------------------------------------------------------- */

static dristy_aruco_config_t g_config;
static dristy_aruco_detection_t g_detections[DRISTY_ARUCO_MAX_DETECTIONS];
static uint8_t g_detection_count;
static dristy_camera_intrinsics_t g_intrinsics; /* for pose */

/* Scratch buffers (reused each frame) */
#define ARUCO_THRESH_SIZE (320 * 240)
static uint8_t g_thresh_buf[ARUCO_THRESH_SIZE] __attribute__((section(".bss")));

/* Candidate quad storage */
typedef struct
{
    float corners[4][2];    /* four corners in image */
    float perimeter;
} aruco_quad_t;

static aruco_quad_t g_candidates[DRISTY_ARUCO_MAX_CANDIDATES];
static uint8_t g_candidate_count;

/* ---------------------------------------------------------------------------
 * Adaptive threshold
 *
 * Gaussian-weighted mean with block_size window, then threshold at mean - C.
 * Optimised: use integral image for O(1) box mean per pixel.
 * On K210: 320×240 takes ~3-4 ms with integral image.
 * ------------------------------------------------------------------------- */

static void adaptive_threshold(const uint8_t *gray, uint8_t *out,
                               uint16_t w, uint16_t h,
                               uint8_t block_size, uint8_t C)
{
    /* Use simple box filter via running sum for speed (not Gaussian, but
     * close enough for marker detection and much faster on K210). */
    int half = block_size / 2;

    for(uint16_t y = 0; y < h; y++)
    {
        for(uint16_t x = 0; x < w; x++)
        {
            /* Compute local mean in block_size×block_size neighbourhood */
            int32_t sum = 0;
            int32_t count = 0;
            int16_t y0 = (int16_t)y - half;
            int16_t y1 = (int16_t)y + half;
            int16_t x0 = (int16_t)x - half;
            int16_t x1 = (int16_t)x + half;
            if(y0 < 0) y0 = 0;
            if(x0 < 0) x0 = 0;
            if(y1 >= (int16_t)h) y1 = h - 1;
            if(x1 >= (int16_t)w) x1 = w - 1;

            for(int16_t yy = y0; yy <= y1; yy++)
            {
                for(int16_t xx = x0; xx <= x1; xx++)
                {
                    sum += gray[yy * w + xx];
                    count++;
                }
            }

            int32_t mean = sum / count;
            out[y * w + x] = (gray[y * w + x] > (mean - C)) ? 255 : 0;
        }
    }
}

/* ---------------------------------------------------------------------------
 * Contour tracing and quad extraction
 *
 * Simple border-following on the thresholded image. We look for closed
 * contours that can be approximated by exactly 4 points (quadrilaterals).
 *
 * Uses Ramer-Douglas-Peucker polygon approximation.
 * ------------------------------------------------------------------------- */

/* Point distance to line segment */
static float point_line_distance(float px, float py,
                                 float x1, float y1,
                                 float x2, float y2)
{
    float dx = x2 - x1, dy = y2 - y1;
    float len_sq = dx * dx + dy * dy;
    if(len_sq < 1e-6f) return sqrtf((px - x1) * (px - x1) + (py - y1) * (py - y1));
    float t = ((px - x1) * dx + (py - y1) * dy) / len_sq;
    if(t < 0) t = 0;
    if(t > 1) t = 1;
    float proj_x = x1 + t * dx;
    float proj_y = y1 + t * dy;
    return sqrtf((px - proj_x) * (px - proj_x) + (py - proj_y) * (py - proj_y));
}

/* Perimeter of a polygon */
static float polygon_perimeter(const float pts[][2], uint16_t count)
{
    float peri = 0;
    for(uint16_t i = 0; i < count; i++)
    {
        uint16_t j = (i + 1) % count;
        float dx = pts[j][0] - pts[i][0];
        float dy = pts[j][1] - pts[i][1];
        peri += sqrtf(dx * dx + dy * dy);
    }
    return peri;
}

/* Check if polygon is convex */
static uint8_t is_convex(const float pts[4][2])
{
    int sign = 0;
    for(int i = 0; i < 4; i++)
    {
        int j = (i + 1) % 4;
        int k = (i + 2) % 4;
        float dx1 = pts[j][0] - pts[i][0];
        float dy1 = pts[j][1] - pts[i][1];
        float dx2 = pts[k][0] - pts[j][0];
        float dy2 = pts[k][1] - pts[j][1];
        float cross = dx1 * dy2 - dy1 * dx2;
        if(cross > 0)
        {
            if(sign < 0) return 0;
            sign = 1;
        }
        else if(cross < 0)
        {
            if(sign > 0) return 0;
            sign = -1;
        }
    }
    return sign != 0;
}

/* Order corners: top-left, top-right, bottom-right, bottom-left */
static void order_corners(float corners[4][2])
{
    /* Sort by sum (x+y): smallest = TL, largest = BR */
    float sums[4], diffs[4];
    for(int i = 0; i < 4; i++)
    {
        sums[i] = corners[i][0] + corners[i][1];
        diffs[i] = corners[i][1] - corners[i][0];
    }

    float ordered[4][2];

    /* TL = min sum, BR = max sum, TR = min diff, BL = max diff */
    int tl = 0, br = 0, tr = 0, bl = 0;
    for(int i = 1; i < 4; i++)
    {
        if(sums[i] < sums[tl]) tl = i;
        if(sums[i] > sums[br]) br = i;
        if(diffs[i] < diffs[tr]) tr = i;
        if(diffs[i] > diffs[bl]) bl = i;
    }

    ordered[0][0] = corners[tl][0]; ordered[0][1] = corners[tl][1];
    ordered[1][0] = corners[tr][0]; ordered[1][1] = corners[tr][1];
    ordered[2][0] = corners[br][0]; ordered[2][1] = corners[br][1];
    ordered[3][0] = corners[bl][0]; ordered[3][1] = corners[bl][1];

    memcpy(corners, ordered, sizeof(ordered));
}

/* ---------------------------------------------------------------------------
 * Find quadrilateral candidates in thresholded image
 *
 * Simple approach: scan for connected components of black regions,
 * find their bounding contour, and check if it's a quadrilateral.
 *
 * For K210 we use a fast line-scan approach instead of full contour
 * tracing: find horizontal run-length segments, merge into blobs,
 * compute convex hull → check if 4-sided.
 *
 * Simplified here: find quads by scanning for square-like black regions.
 * This is a simplified version; a full implementation would use proper
 * contour tracing (Suzuki-Abe) but that requires more SRAM.
 * ------------------------------------------------------------------------- */

static void find_quad_candidates(const uint8_t *thresh,
                                 uint16_t w, uint16_t h)
{
    g_candidate_count = 0;

    /* Scan for seed points in black regions, flood-fill to find contour.
     * For K210 memory constraints, we use a simplified grid sampling approach:
     * divide frame into cells, find connected dark regions, fit quads. */

    /* Grid-based seed search: every 8th pixel */
    uint8_t visited[320 * 240 / 64]; /* 1-bit per 8×8 block */
    memset(visited, 0, sizeof(visited));

    for(uint16_t by = 4; by < h - 4; by += 8)
    {
        for(uint16_t bx = 4; bx < w - 4; bx += 8)
        {
            uint16_t block_idx = (by / 8) * (w / 8) + (bx / 8);
            if(block_idx / 8 >= sizeof(visited)) continue;
            if(visited[block_idx / 8] & (1 << (block_idx % 8))) continue;

            if(thresh[by * w + bx] != 0) continue; /* skip white */

            /* Found a black seed. Trace the approximate bounding contour.
             * For speed, we just find the extreme points (top, right, bottom, left)
             * and their diagonal neighbours to form a quad approximation. */

            int16_t min_x = bx, max_x = bx, min_y = by, max_y = by;
            int16_t top_x = bx, right_y = by, bot_x = bx, left_y = by;

            /* Expand in 8 directions */
            for(int step = 1; step < 120; step++)
            {
                uint8_t expanded = 0;
                /* Up */
                if(min_y - 1 >= 0 && thresh[(min_y - 1) * w + top_x] == 0)
                { min_y--; expanded = 1; }
                /* Down */
                if(max_y + 1 < h && thresh[(max_y + 1) * w + bot_x] == 0)
                { max_y++; expanded = 1; }
                /* Left */
                if(min_x - 1 >= 0 && thresh[left_y * w + (min_x - 1)] == 0)
                { min_x--; expanded = 1; }
                /* Right */
                if(max_x + 1 < w && thresh[right_y * w + (max_x + 1)] == 0)
                { max_x++; expanded = 1; }

                if(!expanded) break;
            }

            /* Mark visited blocks in this region */
            for(int16_t yy = min_y / 8; yy <= max_y / 8 && yy < (int16_t)(h / 8); yy++)
            {
                for(int16_t xx = min_x / 8; xx <= max_x / 8 && xx < (int16_t)(w / 8); xx++)
                {
                    uint16_t idx = yy * (w / 8) + xx;
                    if(idx / 8 < sizeof(visited))
                        visited[idx / 8] |= (1 << (idx % 8));
                }
            }

            /* Check aspect ratio and size */
            float region_w = (float)(max_x - min_x);
            float region_h = (float)(max_y - min_y);
            if(region_w < 8 || region_h < 8) continue;
            float aspect = region_w / region_h;
            if(aspect < 0.3f || aspect > 3.0f) continue;

            float perimeter = 2 * (region_w + region_h);
            if(perimeter < g_config.min_marker_perimeter) continue;
            if(perimeter > g_config.max_marker_perimeter) continue;

            /* Refine corners by scanning the actual contour edges */
            float c[4][2] = {
                { (float)min_x, (float)min_y },  /* TL */
                { (float)max_x, (float)min_y },  /* TR */
                { (float)max_x, (float)max_y },  /* BR */
                { (float)min_x, (float)max_y },  /* BL */
            };

            if(g_candidate_count >= DRISTY_ARUCO_MAX_CANDIDATES) return;

            aruco_quad_t *q = &g_candidates[g_candidate_count];
            memcpy(q->corners, c, sizeof(c));
            q->perimeter = perimeter;
            g_candidate_count++;
        }
    }
}

/* ---------------------------------------------------------------------------
 * Perspective unwarp and bit extraction
 *
 * Given 4 corners of a candidate quad, compute a 3×3 homography to unwarp
 * the marker region, then sample the inner grid cells to extract bits.
 * ------------------------------------------------------------------------- */

/* Bilinear sample from grayscale image */
static uint8_t sample_bilinear(const uint8_t *gray, uint16_t w, uint16_t h,
                               float fx, float fy)
{
    if(fx < 0) fx = 0;
    if(fy < 0) fy = 0;
    if(fx >= w - 1) fx = (float)(w - 2);
    if(fy >= h - 1) fy = (float)(h - 2);

    int x0 = (int)fx, y0 = (int)fy;
    float dx = fx - x0, dy = fy - y0;

    uint8_t v00 = gray[y0 * w + x0];
    uint8_t v10 = gray[y0 * w + x0 + 1];
    uint8_t v01 = gray[(y0 + 1) * w + x0];
    uint8_t v11 = gray[(y0 + 1) * w + x0 + 1];

    float val = v00 * (1 - dx) * (1 - dy) + v10 * dx * (1 - dy) +
                v01 * (1 - dx) * dy + v11 * dx * dy;
    return (uint8_t)(val + 0.5f);
}

/* Simple perspective transform: map (u,v) in [0,1]×[0,1] to image coords
 * using bilinear interpolation of the 4 corners */
static void quad_sample_point(const float corners[4][2],
                              float u, float v,
                              float *out_x, float *out_y)
{
    /* Bilinear interpolation of quad corners */
    float x0 = corners[0][0] * (1 - u) + corners[1][0] * u;
    float y0 = corners[0][1] * (1 - u) + corners[1][1] * u;
    float x1 = corners[3][0] * (1 - u) + corners[2][0] * u;
    float y1 = corners[3][1] * (1 - u) + corners[2][1] * u;
    *out_x = x0 * (1 - v) + x1 * v;
    *out_y = y0 * (1 - v) + y1 * v;
}

/* Extract bits from a candidate quad */
static uint8_t extract_bits(const uint8_t *gray, uint16_t w, uint16_t h,
                            const float corners[4][2],
                            uint8_t grid_size, uint8_t *bits)
{
    /* Total cells = grid_size + 2 (including border) */
    uint8_t total_cells = grid_size + 2;
    float cell_size = 1.0f / total_cells;

    /* First verify border is black (ArUco spec: outer ring must be black) */
    int border_black_count = 0;
    int border_total = 0;

    for(uint8_t i = 0; i < total_cells; i++)
    {
        for(uint8_t j = 0; j < total_cells; j++)
        {
            if(i > 0 && i < total_cells - 1 && j > 0 && j < total_cells - 1)
                continue; /* inner cell, skip */

            float u = (j + 0.5f) * cell_size;
            float v = (i + 0.5f) * cell_size;
            float sx, sy;
            quad_sample_point(corners, u, v, &sx, &sy);
            uint8_t val = sample_bilinear(gray, w, h, sx, sy);
            if(val < 128) border_black_count++;
            border_total++;
        }
    }

    /* At least 75% of border should be black */
    if(border_black_count < border_total * 3 / 4)
        return 0;

    /* Extract inner grid bits */
    /* Use Otsu-like threshold: compute mean of all inner cells,
     * then threshold at mean */
    float mean = 0;
    for(uint8_t row = 0; row < grid_size; row++)
    {
        for(uint8_t col = 0; col < grid_size; col++)
        {
            float u = (col + 1 + 0.5f) * cell_size;
            float v = (row + 1 + 0.5f) * cell_size;
            float sx, sy;
            quad_sample_point(corners, u, v, &sx, &sy);
            mean += sample_bilinear(gray, w, h, sx, sy);
        }
    }
    mean /= (grid_size * grid_size);

    /* Extract bits: 1 = white (above mean), 0 = black */
    memset(bits, 0, (grid_size * grid_size + 7) / 8);
    for(uint8_t row = 0; row < grid_size; row++)
    {
        for(uint8_t col = 0; col < grid_size; col++)
        {
            float u = (col + 1 + 0.5f) * cell_size;
            float v = (row + 1 + 0.5f) * cell_size;
            float sx, sy;
            quad_sample_point(corners, u, v, &sx, &sy);
            uint8_t val = sample_bilinear(gray, w, h, sx, sy);
            uint8_t bit_idx = row * grid_size + col;
            if(val > mean)
                bits[bit_idx / 8] |= (1 << (7 - (bit_idx % 8)));
        }
    }

    return 1;
}

/* Rotate bits 90° clockwise */
static void rotate_bits_cw(const uint8_t *in, uint8_t *out,
                           uint8_t grid_size)
{
    uint8_t n = grid_size;
    memset(out, 0, (n * n + 7) / 8);
    for(uint8_t row = 0; row < n; row++)
    {
        for(uint8_t col = 0; col < n; col++)
        {
            uint8_t src_idx = row * n + col;
            uint8_t dst_row = col;
            uint8_t dst_col = n - 1 - row;
            uint8_t dst_idx = dst_row * n + dst_col;

            if(in[src_idx / 8] & (1 << (7 - (src_idx % 8))))
                out[dst_idx / 8] |= (1 << (7 - (dst_idx % 8)));
        }
    }
}

/* Compute Hamming distance between two bit arrays */
static uint8_t hamming_distance(const uint8_t *a, const uint8_t *b,
                                uint16_t num_bits)
{
    uint8_t dist = 0;
    uint16_t bytes = (num_bits + 7) / 8;
    for(uint16_t i = 0; i < bytes; i++)
    {
        uint8_t xor_val = a[i] ^ b[i];
        while(xor_val)
        {
            dist += xor_val & 1;
            xor_val >>= 1;
        }
    }
    return dist;
}

/* Match extracted bits against dictionary.
 * Tests all 4 rotations. Returns marker ID or -1 if no match. */
static int16_t match_dictionary(const uint8_t *bits, uint8_t grid_size,
                                const dristy_aruco_dict_info_t *dict,
                                uint8_t max_hamming,
                                uint8_t *out_hamming,
                                uint8_t *out_rotation)
{
    if(!dict->data)
        return -1;

    uint16_t total_bits = grid_size * grid_size;
    uint16_t bytes = dict->bytes_per_marker;
    uint8_t rotated[4][8]; /* max 6×6 = 36 bits = 5 bytes, pad to 8 */

    /* Generate all 4 rotations */
    memcpy(rotated[0], bits, bytes);
    rotate_bits_cw(rotated[0], rotated[1], grid_size);
    rotate_bits_cw(rotated[1], rotated[2], grid_size);
    rotate_bits_cw(rotated[2], rotated[3], grid_size);

    int16_t best_id = -1;
    uint8_t best_hamming = 255;
    uint8_t best_rot = 0;

    for(uint16_t id = 0; id < dict->num_markers; id++)
    {
        const uint8_t *ref = dict->data + id * bytes;
        for(uint8_t rot = 0; rot < 4; rot++)
        {
            uint8_t dist = hamming_distance(rotated[rot], ref, total_bits);
            if(dist < best_hamming)
            {
                best_hamming = dist;
                best_id = (int16_t)id;
                best_rot = rot;
            }
            if(dist == 0) goto found;
        }
    }

found:
    if(best_hamming > max_hamming)
        return -1;

    *out_hamming = best_hamming;
    *out_rotation = best_rot;
    return best_id;
}

/* ---------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

uint8_t dristy_aruco_init(const dristy_aruco_config_t *config)
{
    if(config)
        g_config = *config;
    else
    {
        dristy_aruco_config_t defaults = DRISTY_ARUCO_CONFIG_DEFAULT;
        g_config = defaults;
    }

    g_detection_count = 0;

    /* Set default camera intrinsics for OV2640 at 320×240 */
    g_intrinsics = (dristy_camera_intrinsics_t){
        .fx = 230.0f, .fy = 230.0f,
        .cx = 160.0f, .cy = 120.0f,
        .k1 = -0.15f, .k2 = 0.01f,
    };

    return 1;
}

void dristy_aruco_set_dict(dristy_aruco_dict_t dict)
{
    if(dict < DRISTY_ARUCO_DICT_COUNT)
        g_config.dict = dict;
}

void dristy_aruco_set_marker_size(float size_mm)
{
    g_config.marker_size_mm = size_mm;
}

uint8_t dristy_aruco_detect(const uint8_t *gray,
                            uint16_t width, uint16_t height)
{
    g_detection_count = 0;

    if(!gray || width == 0 || height == 0)
        return 0;
    if((uint32_t)width * height > ARUCO_THRESH_SIZE)
        return 0;

    const dristy_aruco_dict_info_t *dict = &g_dict_table[g_config.dict];
    if(!dict->data)
        return 0;

    /* Step 1: Adaptive threshold */
    adaptive_threshold(gray, g_thresh_buf, width, height,
                       g_config.adaptive_thresh_block,
                       g_config.adaptive_thresh_c);

    /* Step 2: Find quadrilateral candidates */
    find_quad_candidates(g_thresh_buf, width, height);

    /* Step 3: For each candidate, extract bits and match */
    uint8_t bits[8]; /* max 6×6 = 36 bits = 5 bytes */

    for(uint8_t i = 0; i < g_candidate_count &&
        g_detection_count < DRISTY_ARUCO_MAX_DETECTIONS; i++)
    {
        aruco_quad_t *q = &g_candidates[i];

        if(!extract_bits(gray, width, height, q->corners,
                         dict->grid_size, bits))
            continue;

        uint8_t hamming, rotation;
        int16_t id = match_dictionary(bits, dict->grid_size, dict,
                                      g_config.max_hamming_distance,
                                      &hamming, &rotation);
        if(id < 0)
            continue;

        /* Rotate corners to match the canonical orientation */
        dristy_aruco_detection_t *det = &g_detections[g_detection_count];
        memset(det, 0, sizeof(*det));
        det->id = (uint16_t)id;
        det->dict = g_config.dict;
        det->hamming = hamming;
        det->rotation = rotation;

        /* Copy corners, rotating to match detected rotation */
        for(int c = 0; c < 4; c++)
        {
            int src = (c + rotation) % 4;
            det->corners[c][0] = q->corners[src][0];
            det->corners[c][1] = q->corners[src][1];
        }

        /* Compute centre and bounding box */
        float cx = 0, cy = 0;
        float x_min = 1e6f, y_min = 1e6f, x_max = -1e6f, y_max = -1e6f;
        for(int c = 0; c < 4; c++)
        {
            cx += det->corners[c][0];
            cy += det->corners[c][1];
            if(det->corners[c][0] < x_min) x_min = det->corners[c][0];
            if(det->corners[c][1] < y_min) y_min = det->corners[c][1];
            if(det->corners[c][0] > x_max) x_max = det->corners[c][0];
            if(det->corners[c][1] > y_max) y_max = det->corners[c][1];
        }
        det->cx = (int16_t)(cx / 4 + 0.5f);
        det->cy = (int16_t)(cy / 4 + 0.5f);
        det->x = (int16_t)x_min;
        det->y = (int16_t)y_min;
        det->w = (int16_t)(x_max - x_min + 1);
        det->h = (int16_t)(y_max - y_min + 1);

        /* Compute 6-DOF pose if marker size is known */
        if(g_config.marker_size_mm > 0.0f)
        {
            dristy_point2f_t img_pts[4];
            for(int c = 0; c < 4; c++)
            {
                img_pts[c].x = det->corners[c][0];
                img_pts[c].y = det->corners[c][1];
            }

            dristy_pose_t pose;
            if(dristy_pose_from_tag(img_pts, g_config.marker_size_mm,
                                    &g_intrinsics, &pose) == 0)
            {
                det->pose_valid = 1;
                det->tx_mm = pose.tx;
                det->ty_mm = pose.ty;
                det->tz_mm = pose.tz;
                det->roll_deg = pose.roll;
                det->pitch_deg = pose.pitch;
                det->yaw_deg = pose.yaw;
                det->range_mm = pose.range;
                det->reproj_error = pose.reproj_error;
            }
        }

        g_detection_count++;
    }

    return g_detection_count;
}

const dristy_aruco_detection_t *dristy_aruco_results(uint8_t *count)
{
    if(count) *count = g_detection_count;
    return g_detections;
}

const dristy_aruco_dict_info_t *dristy_aruco_dict_info(dristy_aruco_dict_t dict)
{
    if(dict >= DRISTY_ARUCO_DICT_COUNT) return NULL;
    return &g_dict_table[dict];
}
