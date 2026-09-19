#ifndef QR_DECODER_ENGINE_H
#define QR_DECODER_ENGINE_H

#include <stdint.h>


typedef enum
{
    QR_ENGINE_FOUND = 0,
    QR_ENGINE_NOT_FOUND,
    QR_ENGINE_DECODER_FAIL,
} qr_engine_result_t;

typedef struct
{
    const uint16_t *pixels;
    uint16_t width;
    uint16_t height;
} qr_engine_frame_t;

typedef struct
{
    uint8_t decoder_ready;
    uint32_t decode_count;
    int code_count;
    uint8_t last_decode_ok;
    uint8_t luma_min;
    uint8_t luma_avg;
    uint8_t luma_max;
} qr_engine_stats_t;

void qr_decoder_engine_reset_session(void);
qr_engine_result_t qr_decoder_engine_decode(const qr_engine_frame_t *frame, uint8_t force);
void qr_decoder_engine_stats(qr_engine_stats_t *stats);

#endif
