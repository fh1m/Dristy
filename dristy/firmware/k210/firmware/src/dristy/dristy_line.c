#include "dristy_line.h"

void dristy_line_init(void)
{
}

void dristy_line_process_gray(const uint8_t *gray, uint16_t w, uint16_t h,
                              dristy_result_snapshot_t *snap)
{
    int64_t sum_x = 0;
    int64_t sum_y = 0;
    int64_t sum_w = 0;
    int16_t y0 = (int16_t)(h * 2U / 3U);
    int16_t y1 = (int16_t)h;
    int16_t x_left = (int16_t)w;
    int16_t x_right = 0;

    if(!gray || !snap || w < 8U || h < 8U)
        return;

    for(int16_t y = y0; y < y1; y++)
    {
        for(int16_t x = 0; x < (int16_t)w; x++)
        {
            if(gray[(uint32_t)y * w + (uint32_t)x] < 80U)
            {
                sum_x += x;
                sum_y += y;
                sum_w++;
                if(x < x_left)
                    x_left = x;
                if(x > x_right)
                    x_right = x;
            }
        }
    }

    snap->line_valid = 0U;
    if(sum_w < 40)
        return;

    {
        int16_t cx = (int16_t)(sum_x / sum_w);
        int16_t cy = (int16_t)(sum_y / sum_w);
        dristy_result_line_t *line = &snap->line;

        line->x1 = dristy_pixel_to_norm_x(x_left, w);
        line->y1 = dristy_pixel_to_norm_y((int16_t)y1, h);
        line->x2 = dristy_pixel_to_norm_x(x_right, w);
        line->y2 = dristy_pixel_to_norm_y((int16_t)y0, h);
        line->offset = dristy_pixel_to_norm_x(cx, w);
        line->angle_x10 = (int16_t)(900 - (x_right - x_left) * 900 / (int16_t)w);
        line->curvature_x100 = 0;
        snap->line_valid = 1U;
        snap->target.type = DRISTY_TARGET_LINE;
        snap->target.error_x = line->offset;
        snap->target.error_y = dristy_pixel_to_norm_y(cy, h);
        snap->target.confidence = (uint16_t)(sum_w > 500 ? 900U : 500U);
    }
}
