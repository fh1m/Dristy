#ifndef QR_LUMA_H
#define QR_LUMA_H

#include <stdint.h>

typedef struct
{
    uint8_t min;
    uint8_t avg;
    uint8_t max;
} qr_luma_stats_t;

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
                        qr_luma_stats_t *stats);

#endif
