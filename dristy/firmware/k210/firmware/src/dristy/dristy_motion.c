#include "dristy_motion.h"

#include <string.h>

/* ---------------------------------------------------------------------------
 * Motion detection with adaptive background model
 *
 * Works at quarter resolution (160×120) for speed. Each pixel in the
 * background model is a uint8_t EMA. Frame differencing produces a
 * binary motion mask which is cleaned with morphological operations.
 * ------------------------------------------------------------------------- */

#define MOTION_QW 160
#define MOTION_QH 120
#define MOTION_BUF_SIZE (MOTION_QW * MOTION_QH)

static dristy_motion_config_t g_config;
static dristy_motion_result_t g_result;

/* Background model (EMA) */
static uint8_t g_background[MOTION_BUF_SIZE] __attribute__((section(".bss")));
static uint8_t g_diff_buf[MOTION_BUF_SIZE] __attribute__((section(".bss")));
static uint8_t g_mask_buf[MOTION_BUF_SIZE] __attribute__((section(".bss")));
static uint8_t g_bg_initialized;

/* Downsample full-res grayscale to quarter-res */
static void downsample_2x(const uint8_t *src, uint8_t *dst,
                          uint16_t src_w, uint16_t src_h)
{
    uint16_t dst_w = src_w / 2;
    uint16_t dst_h = src_h / 2;
    for(uint16_t y = 0; y < dst_h; y++)
    {
        for(uint16_t x = 0; x < dst_w; x++)
        {
            uint16_t sy = y * 2, sx = x * 2;
            uint16_t v = (uint16_t)src[sy * src_w + sx] +
                         src[sy * src_w + sx + 1] +
                         src[(sy + 1) * src_w + sx] +
                         src[(sy + 1) * src_w + sx + 1];
            dst[y * dst_w + x] = (uint8_t)(v / 4);
        }
    }
}

/* Update background model: bg = alpha * current + (1-alpha) * bg */
static void update_background(const uint8_t *current, uint16_t w, uint16_t h)
{
    /* alpha in Q8: 0.05 ≈ 13/256 */
    uint8_t alpha_q8 = (uint8_t)(g_config.bg_alpha * 256);
    uint16_t one_minus_alpha = 256 - alpha_q8;
    uint32_t total = (uint32_t)w * h;

    for(uint32_t i = 0; i < total; i++)
    {
        g_background[i] = (uint8_t)(
            ((uint16_t)current[i] * alpha_q8 +
             (uint16_t)g_background[i] * one_minus_alpha) >> 8);
    }
}

/* Compute absolute difference */
static void frame_diff(const uint8_t *current, const uint8_t *bg,
                       uint8_t *diff, uint16_t w, uint16_t h)
{
    uint32_t total = (uint32_t)w * h;
    for(uint32_t i = 0; i < total; i++)
    {
        int16_t d = (int16_t)current[i] - (int16_t)bg[i];
        diff[i] = (uint8_t)(d < 0 ? -d : d);
    }
}

/* Threshold diff into binary mask */
static void threshold_mask(const uint8_t *diff, uint8_t *mask,
                           uint32_t count, uint8_t thresh)
{
    for(uint32_t i = 0; i < count; i++)
        mask[i] = diff[i] > thresh ? 255 : 0;
}

/* 3×3 erode (shrink white regions) */
static void erode_3x3(uint8_t *mask, uint16_t w, uint16_t h)
{
    /* In-place with temporary row buffer */
    uint8_t prev_row[MOTION_QW];
    uint8_t curr_row[MOTION_QW];

    for(uint16_t y = 1; y < h - 1; y++)
    {
        memcpy(prev_row, mask + (y - 1) * w, w);
        memcpy(curr_row, mask + y * w, w);
        for(uint16_t x = 1; x < w - 1; x++)
        {
            /* Erode: pixel is white only if ALL 8 neighbours are white */
            if(prev_row[x-1] && prev_row[x] && prev_row[x+1] &&
               curr_row[x-1] && curr_row[x] && curr_row[x+1] &&
               mask[(y+1)*w+x-1] && mask[(y+1)*w+x] && mask[(y+1)*w+x+1])
                g_diff_buf[y * w + x] = 255; /* reuse diff_buf as temp */
            else
                g_diff_buf[y * w + x] = 0;
        }
    }
    /* Copy back */
    for(uint16_t y = 1; y < h - 1; y++)
        memcpy(mask + y * w + 1, g_diff_buf + y * w + 1, w - 2);
}

/* 3×3 dilate (grow white regions) */
static void dilate_3x3(uint8_t *mask, uint16_t w, uint16_t h)
{
    for(uint16_t y = 1; y < h - 1; y++)
    {
        for(uint16_t x = 1; x < w - 1; x++)
        {
            /* Dilate: pixel is white if ANY neighbour is white */
            if(mask[(y-1)*w+x-1] || mask[(y-1)*w+x] || mask[(y-1)*w+x+1] ||
               mask[y*w+x-1]     || mask[y*w+x]     || mask[y*w+x+1]     ||
               mask[(y+1)*w+x-1] || mask[(y+1)*w+x] || mask[(y+1)*w+x+1])
                g_diff_buf[y * w + x] = 255;
            else
                g_diff_buf[y * w + x] = 0;
        }
    }
    for(uint16_t y = 1; y < h - 1; y++)
        memcpy(mask + y * w + 1, g_diff_buf + y * w + 1, w - 2);
}

/* Simple connected component bounding boxes via scanline flood */
static uint8_t find_motion_regions(const uint8_t *mask, uint16_t w, uint16_t h,
                                   const uint8_t *diff, uint8_t scale)
{
    uint8_t count = 0;
    /* Simple grid-based region finder: divide into 8×8 blocks,
     * find blocks with enough motion, merge adjacent blocks */
    uint8_t block_w = w / 8, block_h = h / 8;
    uint8_t block_motion[20][15]; /* max 160/8 × 120/8 = 20×15 */
    memset(block_motion, 0, sizeof(block_motion));

    for(uint16_t by = 0; by < block_h && by < 15; by++)
    {
        for(uint16_t bx = 0; bx < block_w && bx < 20; bx++)
        {
            uint32_t motion_pixels = 0;
            for(uint16_t y = by * 8; y < (by + 1) * 8 && y < h; y++)
                for(uint16_t x = bx * 8; x < (bx + 1) * 8 && x < w; x++)
                    if(mask[y * w + x]) motion_pixels++;

            if(motion_pixels > 8) /* at least 8 pixels per block */
                block_motion[bx][by] = 1;
        }
    }

    /* Simple single-pass: each connected group of motion blocks = 1 region */
    uint8_t visited[20][15];
    memset(visited, 0, sizeof(visited));

    for(uint16_t by = 0; by < block_h && by < 15; by++)
    {
        for(uint16_t bx = 0; bx < block_w && bx < 20; bx++)
        {
            if(!block_motion[bx][by] || visited[bx][by])
                continue;
            if(count >= DRISTY_MOTION_MAX_REGIONS)
                return count;

            /* Flood fill this region */
            uint16_t x_min = bx, x_max = bx, y_min = by, y_max = by;
            uint32_t total_pixels = 0;

            /* Simple stack-less flood: expand in all directions */
            uint8_t expanded = 1;
            visited[bx][by] = 1;
            while(expanded)
            {
                expanded = 0;
                for(uint16_t yy = y_min; yy <= y_max && yy < 15; yy++)
                {
                    for(uint16_t xx = x_min; xx <= x_max && xx < 20; xx++)
                    {
                        if(!visited[xx][yy] || !block_motion[xx][yy])
                            continue;
                        /* Check neighbours */
                        for(int dy = -1; dy <= 1; dy++)
                        {
                            for(int dx = -1; dx <= 1; dx++)
                            {
                                int nx = (int)xx + dx, ny = (int)yy + dy;
                                if(nx < 0 || ny < 0 || nx >= 20 || ny >= 15)
                                    continue;
                                if(block_motion[nx][ny] && !visited[nx][ny])
                                {
                                    visited[nx][ny] = 1;
                                    if((uint16_t)nx < x_min) x_min = nx;
                                    if((uint16_t)nx > x_max) x_max = nx;
                                    if((uint16_t)ny < y_min) y_min = ny;
                                    if((uint16_t)ny > y_max) y_max = ny;
                                    expanded = 1;
                                }
                            }
                        }
                    }
                }
            }

            /* Count actual motion pixels in this region */
            for(uint16_t yy = y_min * 8; yy < (y_max + 1) * 8 && yy < h; yy++)
                for(uint16_t xx = x_min * 8; xx < (x_max + 1) * 8 && xx < w; xx++)
                    if(mask[yy * w + xx]) total_pixels++;

            if(total_pixels < g_config.min_region_area)
                continue;

            /* Compute mean intensity in this region */
            float sum_intensity = 0;
            for(uint16_t yy = y_min * 8; yy < (y_max + 1) * 8 && yy < h; yy++)
                for(uint16_t xx = x_min * 8; xx < (x_max + 1) * 8 && xx < w; xx++)
                    if(mask[yy * w + xx])
                        sum_intensity += diff[yy * w + xx];

            dristy_motion_region_t *r = &g_result.regions[count];
            r->x = (int16_t)(x_min * 8 * scale);
            r->y = (int16_t)(y_min * 8 * scale);
            r->w = (int16_t)((x_max - x_min + 1) * 8 * scale);
            r->h = (int16_t)((y_max - y_min + 1) * 8 * scale);
            r->pixel_count = total_pixels * scale * scale;
            r->intensity = total_pixels > 0 ?
                sum_intensity / (total_pixels * 255.0f) : 0;
            count++;
        }
    }

    return count;
}

void dristy_motion_init(const dristy_motion_config_t *config)
{
    if(config)
        g_config = *config;
    else
    {
        dristy_motion_config_t defaults = DRISTY_MOTION_CONFIG_DEFAULT;
        g_config = defaults;
    }
    g_bg_initialized = 0;
    memset(&g_result, 0, sizeof(g_result));
}

uint8_t dristy_motion_process(const uint8_t *gray,
                              uint16_t width, uint16_t height)
{
    uint16_t proc_w, proc_h;
    uint8_t scale;
    const uint8_t *proc_frame;
    static uint8_t quarter_buf[MOTION_BUF_SIZE];

    if(!gray) return 0;

    if(g_config.quarter_res && width >= 320 && height >= 240)
    {
        downsample_2x(gray, quarter_buf, width, height);
        proc_w = width / 2;
        proc_h = height / 2;
        proc_frame = quarter_buf;
        scale = 2;
    }
    else
    {
        proc_frame = gray;
        proc_w = width;
        proc_h = height;
        scale = 1;
    }

    uint32_t total = (uint32_t)proc_w * proc_h;
    if(total > MOTION_BUF_SIZE)
        return 0;

    /* First frame: initialise background */
    if(!g_bg_initialized)
    {
        memcpy(g_background, proc_frame, total);
        g_bg_initialized = 1;
        memset(&g_result, 0, sizeof(g_result));
        return 0;
    }

    /* Frame differencing */
    frame_diff(proc_frame, g_background, g_diff_buf, proc_w, proc_h);

    /* Threshold */
    threshold_mask(g_diff_buf, g_mask_buf, total, g_config.diff_threshold);

    /* Morphological cleanup */
    for(uint8_t i = 0; i < g_config.erode_iterations; i++)
        erode_3x3(g_mask_buf, proc_w, proc_h);
    for(uint8_t i = 0; i < g_config.dilate_iterations; i++)
        dilate_3x3(g_mask_buf, proc_w, proc_h);

    /* Count motion pixels and compute stats */
    uint32_t motion_count = 0;
    float mean_diff = 0;
    for(uint32_t i = 0; i < total; i++)
    {
        if(g_mask_buf[i])
        {
            motion_count++;
            mean_diff += g_diff_buf[i];
        }
    }

    g_result.motion_percent = (float)motion_count * 100.0f / total;
    g_result.mean_intensity = motion_count > 0 ?
        mean_diff / (motion_count * 255.0f) : 0;
    g_result.triggered = g_result.motion_percent > g_config.motion_threshold;

    /* Find regions */
    g_result.region_count = find_motion_regions(
        g_mask_buf, proc_w, proc_h, g_diff_buf, scale);

    /* Update background model */
    update_background(proc_frame, proc_w, proc_h);

    return g_result.triggered;
}

const dristy_motion_result_t *dristy_motion_result(void)
{
    return &g_result;
}

void dristy_motion_reset_background(void)
{
    g_bg_initialized = 0;
}

void dristy_motion_set_threshold(float percent)
{
    g_config.motion_threshold = percent;
}

void dristy_motion_set_sensitivity(uint8_t diff_thresh)
{
    g_config.diff_threshold = diff_thresh;
}
