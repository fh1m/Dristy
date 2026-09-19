#include "hk_camera_sizes.h"

#include "camera_types.h"

static const camera_size_t g_safe_camera_sizes[] = {
    CAMERA_SIZE_160X120,
    CAMERA_SIZE_320X240,
    CAMERA_SIZE_640X480,
};

const char *camera_size_label(camera_size_t size)
{
    if(size == CAMERA_SIZE_160X120)
        return "160x120";
    if(size == CAMERA_SIZE_640X480)
        return "640x480";
    return "320x240";
}

uint16_t camera_size_width(camera_size_t size)
{
    if(size == CAMERA_SIZE_160X120)
        return 160;
    if(size == CAMERA_SIZE_640X480)
        return 640;
    return 320;
}

uint16_t camera_size_height(camera_size_t size)
{
    if(size == CAMERA_SIZE_160X120)
        return 120;
    if(size == CAMERA_SIZE_640X480)
        return 480;
    return 240;
}

uint8_t camera_size_is_safe(camera_size_t size)
{
    for(uint8_t i = 0; i < sizeof(g_safe_camera_sizes) / sizeof(g_safe_camera_sizes[0]); i++)
    {
        if(g_safe_camera_sizes[i] == size)
            return 1;
    }
    return 0;
}

camera_size_t camera_size_next(camera_size_t current)
{
    const camera_size_t *sizes = g_safe_camera_sizes;
    uint8_t count = (uint8_t)(sizeof(g_safe_camera_sizes) / sizeof(g_safe_camera_sizes[0]));

    for(uint8_t i = 0; i < count; i++)
    {
        if(sizes[i] == current)
            return sizes[(uint8_t)((i + 1U) % count)];
    }
    return sizes[0];
}
