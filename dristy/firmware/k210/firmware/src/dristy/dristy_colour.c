#include "dristy_colour.h"

#include <string.h>

static uint8_t g_l_min = 20U;
static uint8_t g_l_max = 245U;
static uint8_t g_a_min = 0U;
static uint8_t g_a_max = 255U;
static uint8_t g_b_min = 0U;
static uint8_t g_b_max = 255U;

void dristy_colour_init(void)
{
}

void dristy_colour_set_thresholds(uint8_t l_min, uint8_t l_max,
                                  uint8_t a_min, uint8_t a_max,
                                  uint8_t b_min, uint8_t b_max)
{
    g_l_min = l_min;
    g_l_max = l_max > l_min ? l_max : (uint8_t)(l_min + 1U);
    g_a_min = a_min;
    g_a_max = a_max > a_min ? a_max : (uint8_t)(a_min + 1U);
    g_b_min = b_min;
    g_b_max = b_max > b_min ? b_max : (uint8_t)(b_min + 1U);
}

static uint8_t pixel_match(uint16_t p)
{
    uint8_t r = (uint8_t)(((p >> 11) & 0x1FU) << 3);
    uint8_t g = (uint8_t)(((p >> 5) & 0x3FU) << 2);
    uint8_t b = (uint8_t)((p & 0x1FU) << 3);
    uint8_t l = (uint8_t)((r * 77U + g * 150U + b * 29U) >> 8);
    int16_t cr = (int16_t)r - (int16_t)g;
    int16_t cb = (int16_t)b - (int16_t)g;

    if(l < g_l_min || l > g_l_max)
        return 0U;
    if((uint8_t)(cr + 128) < g_a_min || (uint8_t)(cr + 128) > g_a_max)
        return 0U;
    if((uint8_t)(cb + 128) < g_b_min || (uint8_t)(cb + 128) > g_b_max)
        return 0U;
    return 1U;
}

typedef struct
{
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    uint32_t pixels;
} blob_box_t;

void dristy_colour_process_rgb565(const volatile uint16_t *rgb, uint16_t w, uint16_t h,
                                  dristy_result_snapshot_t *snap, uint8_t sort_by_area)
{
    static uint8_t visited[320U * 240U / 64U];
    blob_box_t boxes[DRISTY_MAX_BLOBS];
    uint8_t box_count = 0U;
    uint32_t cells = (uint32_t)w * (uint32_t)h;
    uint32_t i;

    if(!rgb || !snap || w > 320U || h > 240U)
        return;
    if(cells > sizeof(visited) * 64U)
        return;
    memset(visited, 0, (cells + 63U) / 64U);

    for(uint16_t y = 0U; y < h && box_count < DRISTY_MAX_BLOBS; y += 4U)
    {
        for(uint16_t x = 0U; x < w && box_count < DRISTY_MAX_BLOBS; x += 4U)
        {
            uint32_t idx = (uint32_t)y * w + x;
            uint8_t bit = (uint8_t)(idx & 63U);
            uint32_t word = idx >> 6;
            blob_box_t *box;

            if(visited[word] & (1UL << bit))
                continue;
            if(!pixel_match(rgb[idx]))
                continue;

            box = &boxes[box_count];
            box->x = (int16_t)x;
            box->y = (int16_t)y;
            box->w = 4;
            box->h = 4;
            box->pixels = 1U;
            visited[word] |= (1UL << bit);

            for(uint16_t yy = y; yy < h && yy < y + 48U; yy += 2U)
            {
                for(uint16_t xx = x; xx < w && xx < x + 48U; xx += 2U)
                {
                    uint32_t j = (uint32_t)yy * w + xx;
                    if(!pixel_match(rgb[j]))
                        continue;
                    if(xx < (uint16_t)box->x)
                    {
                        box->w += (int16_t)(box->x - xx);
                        box->x = (int16_t)xx;
                    }
                    if(yy < (uint16_t)box->y)
                    {
                        box->h += (int16_t)(box->y - yy);
                        box->y = (int16_t)yy;
                    }
                    if(xx + 2 > (uint16_t)(box->x + box->w))
                        box->w = (int16_t)(xx + 2 - box->x);
                    if(yy + 2 > (uint16_t)(box->y + box->h))
                        box->h = (int16_t)(yy + 2 - box->y);
                    box->pixels++;
                }
            }
            if(box->w >= 8 && box->h >= 8 && box->pixels >= 16U)
                box_count++;
        }
    }

    if(sort_by_area)
    {
        for(uint8_t a = 0U; a + 1U < box_count; a++)
        {
            for(uint8_t b = a + 1U; b < box_count; b++)
            {
                if(boxes[b].pixels > boxes[a].pixels)
                {
                    blob_box_t tmp = boxes[a];
                    boxes[a] = boxes[b];
                    boxes[b] = tmp;
                }
            }
        }
    }

    snap->blob_count = box_count;
    for(i = 0U; i < box_count; i++)
    {
        dristy_result_blob_t *b = &snap->blobs[i];
        int16_t cx = (int16_t)(boxes[i].x + boxes[i].w / 2);
        int16_t cy = (int16_t)(boxes[i].y + boxes[i].h / 2);

        b->cx = cx;
        b->cy = cy;
        b->w = boxes[i].w;
        b->h = boxes[i].h;
        b->norm_cx = dristy_pixel_to_norm_x(cx, w);
        b->norm_cy = dristy_pixel_to_norm_y(cy, h);
        b->pixel_count = boxes[i].pixels;
        b->colour_id = 0U;
        b->density = (float)boxes[i].pixels /
                     (float)((int32_t)boxes[i].w * boxes[i].h);
        b->roundness = 0.5f;
    }
}
