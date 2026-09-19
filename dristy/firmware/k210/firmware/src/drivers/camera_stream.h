#ifndef CAMERA_STREAM_H
#define CAMERA_STREAM_H

#include <stdint.h>

typedef enum
{
    CAMERA_STREAM_STOPPED = 0,
    CAMERA_STREAM_SYNCING,
    CAMERA_STREAM_WAITING,
    CAMERA_STREAM_CAPTURING,
    CAMERA_STREAM_PAUSED,
} camera_stream_state_t;

typedef struct
{
    const volatile uint16_t *pixels;
    uint32_t lease_id;
    uint32_t sequence;
} camera_stream_frame_t;

typedef struct
{
    uint8_t running;
    uint8_t paused;
    uint8_t have_frame;
    uint8_t state;
    uint32_t irq_start_count;
    uint32_t irq_finish_count;
    uint32_t convert_count;
    uint32_t captured_count;
    uint32_t ready_drop_count;
    uint32_t busy_drop_count;
    uint32_t last_status;
    uint32_t last_sequence;
} camera_stream_status_t;

typedef void (*camera_stream_convert_start_hook_t)(void *context);
typedef void (*camera_stream_convert_finish_hook_t)(uint32_t sequence,
                                                    void *context);

uint8_t camera_stream_start(uint16_t width, uint16_t height, uint8_t burst);
void camera_stream_stop(void);
void camera_stream_pause(void);
void camera_stream_resume(void);
uint8_t camera_stream_acquire_latest(camera_stream_frame_t *frame);
void camera_stream_release(uint32_t lease_id);
uint8_t camera_stream_have_frame(void);
uint32_t camera_stream_frame_bytes(void);
void camera_stream_status(camera_stream_status_t *status);
void camera_stream_set_convert_hooks(
    camera_stream_convert_start_hook_t start,
    camera_stream_convert_finish_hook_t finish,
    void *context);
void camera_stream_clear_convert_hooks(void *context);

#endif
