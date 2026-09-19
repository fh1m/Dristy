#include "image_decode.h"

#include <string.h>

#include "../../core/camera_types.h"
#include "../../storage/fat32_types.h"
#include "files_image_config.h"
#include "../../core/hk_camera_sizes.h"

file_result_t files_open_raw565(const fat_file_entry_t *entry, const file_image_sink_t *sink)
{
    fat_stream_t stream;
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t matched = 0;
    uint8_t *row = image_decode_row_buffer();
    uint32_t row_capacity = image_decode_row_buffer_size();

    memset(&stream, 0, sizeof(stream));
    for(uint8_t i = 0; i < CAMERA_SIZE_COUNT; i++)
    {
        uint16_t w = camera_size_width((camera_size_t)i);
        uint16_t h = camera_size_height((camera_size_t)i);
        if(w <= PHOTO_VIEW_MAX_W && h <= PHOTO_VIEW_MAX_H && entry->size == (uint32_t)w * h * 2U)
        {
            width = w;
            height = h;
            matched = 1;
            break;
        }
    }

    if(!sink || !sink->render_row_span)
        return FILE_RESULT_NOT_FOUND;
    if(!matched)
        return FILE_RESULT_UNSUPPORTED_FORMAT;
    if(!fat_stream_open(&stream, entry, 0))
        return FILE_RESULT_READ_ERROR;

    if((uint32_t)width * 2U > row_capacity)
        return FILE_RESULT_TOO_LARGE;

    if(sink->begin)
        sink->begin(sink->context);
    for(uint32_t src_y = 0; src_y < height; src_y++)
    {
        if(!fat_stream_read(&stream, row, (uint32_t)width * 2U))
            return FILE_RESULT_READ_ERROR;
        sink->render_row_span(sink->context, src_y, height, row, width, 16, 0);
    }

    return FILE_RESULT_OK;
}
