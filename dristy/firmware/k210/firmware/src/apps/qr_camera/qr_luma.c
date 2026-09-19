#include "qr_luma.h"

#include "qr_config.h"


static uint8_t qr_rgb565_luma(uint16_t color)
{
    uint8_t r = (uint8_t)((color >> 8) & 0xF8U);
    uint8_t g = (uint8_t)((color >> 3) & 0xFCU);
    uint8_t b = (uint8_t)((color << 3) & 0xF8U);
    return (uint8_t)(((uint16_t)r * 30U + (uint16_t)g * 59U + (uint16_t)b * 11U) / 100U);
}

void qr_luma_fill_image(const uint16_t *src,
                        uint8_t *image,
                        uint16_t width,
                        uint16_t height,
                        uint16_t src_stride,
                        uint16_t src_x0,
                        uint16_t src_y0,
                        uint16_t src_w,
                        uint16_t src_h,
                        uint8_t mode,
                        uint8_t update_stats,
                        qr_luma_stats_t *stats)
{
    uint32_t sum = 0;
    uint8_t min = stats ? stats->min : 0;
    uint8_t max = stats ? stats->max : 0;
    uint16_t range = max > min ? (uint16_t)(max - min) : 0;

    if(update_stats)
    {
        min = 255;
        max = 0;
    }

    for(uint16_t y = 0; y < height; y++)
    {
        uint16_t src_y = (uint16_t)(src_y0 + (((uint32_t)y * src_h) / height));
        for(uint16_t x = 0; x < width; x++)
        {
            uint16_t src_x = (uint16_t)(src_x0 + (((uint32_t)x * src_w) / width));
            uint8_t luma = qr_rgb565_luma(src[(uint32_t)src_y * src_stride + src_x]);

            if(update_stats)
            {
                if(luma < min)
                    min = luma;
                if(luma > max)
                    max = luma;
                sum += luma;
            }
            else if((mode & QR_PASS_STRETCH) && range > 8U)
            {
                int16_t shifted = (int16_t)luma - (int16_t)min;
                if(shifted < 0)
                    shifted = 0;
                if(shifted > range)
                    shifted = (int16_t)range;
                luma = (uint8_t)((uint16_t)shifted * 255U / range);
            }

            if(mode & QR_PASS_INVERT)
                luma = (uint8_t)(255U - luma);
            image[(uint32_t)y * width + x] = luma;
        }
    }

    if(update_stats && stats)
    {
        stats->min = min;
        stats->max = max;
        stats->avg = (uint8_t)(sum / ((uint32_t)width * height));
    }
}
