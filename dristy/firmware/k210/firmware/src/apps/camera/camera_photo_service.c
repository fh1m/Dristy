#include "camera_photo_service.h"

#include <stddef.h>

#include "../../services/camera_frame.h"
#include "../../services/camera_persist_settings.h"
#include "camera_photo_writer.h"

static uint16_t photo_service_read_pixel(void *ctx, uint32_t x, uint32_t y)
{
    (void)ctx;
    return camera_service_frame_pixel(x, y);
}

uint8_t photo_service_save_current_frame(char *saved_name, size_t saved_name_size)
{
    photo_source_t source;

    source.format = camera_service_photo_format();
    camera_service_frame_info(&source.width, &source.height);
    source.read_pixel = photo_service_read_pixel;
    source.ctx = NULL;

    return photo_save_frame(&source, saved_name, saved_name_size);
}
