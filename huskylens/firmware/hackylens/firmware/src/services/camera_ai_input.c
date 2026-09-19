#include "camera_ai_input.h"

#include <stddef.h>
#include <string.h>

#include "../drivers/camera_stream.h"
#include "hal_dvp.h"

#define CAMERA_AI_MAX_WIDTH 320U
#define CAMERA_AI_MAX_HEIGHT 240U
#define CAMERA_AI_MAX_INPUT_BYTES (CAMERA_AI_MAX_WIDTH * CAMERA_AI_MAX_HEIGHT * 3U)
#define CAMERA_AI_DVP_GUARD_BYTES CAMERA_AI_MAX_WIDTH

typedef enum
{
    CAMERA_AI_INPUT_DISABLED = 0,
    CAMERA_AI_INPUT_ARMED,
    CAMERA_AI_INPUT_CAPTURING,
    CAMERA_AI_INPUT_READY,
    CAMERA_AI_INPUT_BUSY,
} camera_ai_input_state_t;

static uint8_t g_camera_ai_input[CAMERA_AI_MAX_INPUT_BYTES + CAMERA_AI_DVP_GUARD_BYTES]
    __attribute__((aligned(256)));
static const void *g_owner;
static uint16_t g_width;
static uint16_t g_height;
static uint32_t g_input_bytes;
static volatile camera_ai_input_state_t g_state;
static volatile uint32_t g_sequence;

static uint8_t *camera_ai_uncached(void)
{
    uintptr_t address = (uintptr_t)g_camera_ai_input;

    if(address >= 0x80000000UL && address < 0x80600000UL)
        address -= 0x40000000UL;
    return (uint8_t *)address;
}

static void camera_ai_convert_start(void *context)
{
    if(context == g_owner && g_state == CAMERA_AI_INPUT_ARMED)
    {
        hal_dvp_output_ai(1U);
        g_state = CAMERA_AI_INPUT_CAPTURING;
        __sync_synchronize();
    }
}

static void camera_ai_convert_finish(uint32_t sequence, void *context)
{
    if(context == g_owner && g_state == CAMERA_AI_INPUT_CAPTURING)
    {
        hal_dvp_output_ai(0U);
        g_sequence = sequence;
        __sync_synchronize();
        g_state = CAMERA_AI_INPUT_READY;
    }
}

uint8_t *camera_ai_input_acquire(const void *owner,
                                 uint16_t width,
                                 uint16_t height,
                                 uint32_t input_bytes)
{
    uint32_t logical_bytes = (uint32_t)width * height * 3U;

    if(!owner || !width || !height || width > CAMERA_AI_MAX_WIDTH ||
       height > CAMERA_AI_MAX_HEIGHT || input_bytes != logical_bytes ||
       input_bytes > CAMERA_AI_MAX_INPUT_BYTES ||
       (g_owner && g_owner != owner))
        return NULL;
    g_owner = owner;
    g_width = width;
    g_height = height;
    g_input_bytes = input_bytes;
    g_state = CAMERA_AI_INPUT_DISABLED;
    g_sequence = 0U;
    memset(camera_ai_uncached(), 0, input_bytes + width);
    return camera_ai_uncached();
}

uint8_t *camera_ai_input_data(const void *owner)
{
    return owner && owner == g_owner ? camera_ai_uncached() : NULL;
}

uint8_t camera_ai_input_attach(const void *owner)
{
    uint8_t *input = camera_ai_input_data(owner);
    uint32_t plane_bytes;

    if(!input || !g_width || !g_height)
        return 0U;
    plane_bytes = (uint32_t)g_width * g_height;
    if(plane_bytes * 3U != g_input_bytes)
        return 0U;
    hal_dvp_set_ai_rgb888((uint32_t)(uintptr_t)input,
                          (uint32_t)(uintptr_t)(input + plane_bytes),
                          (uint32_t)(uintptr_t)(input + plane_bytes * 2U));
    hal_dvp_output_ai(0U);
    camera_stream_set_convert_hooks(camera_ai_convert_start,
                                    camera_ai_convert_finish,
                                    (void *)owner);
    g_sequence = 0U;
    __sync_synchronize();
    g_state = CAMERA_AI_INPUT_ARMED;
    return 1U;
}

uint8_t camera_ai_input_take(const void *owner, uint32_t *sequence)
{
    if(!owner || owner != g_owner || g_state != CAMERA_AI_INPUT_READY)
        return 0U;
    __sync_synchronize();
    if(sequence)
        *sequence = g_sequence;
    g_state = CAMERA_AI_INPUT_BUSY;
    return 1U;
}

void camera_ai_input_arm(const void *owner)
{
    if(!owner || owner != g_owner)
        return;
    hal_dvp_output_ai(0U);
    g_sequence = 0U;
    __sync_synchronize();
    g_state = CAMERA_AI_INPUT_ARMED;
}

void camera_ai_input_cancel(const void *owner)
{
    if(!owner || owner != g_owner)
        return;
    hal_dvp_output_ai(0U);
    g_sequence = 0U;
    __sync_synchronize();
    g_state = CAMERA_AI_INPUT_DISABLED;
}

void camera_ai_input_release(const void *owner)
{
    if(!owner || owner != g_owner)
        return;
    camera_ai_input_cancel(owner);
    camera_stream_clear_convert_hooks((void *)owner);
    g_owner = NULL;
    g_width = 0U;
    g_height = 0U;
    g_input_bytes = 0U;
}
