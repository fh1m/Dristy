#include "image_decode.h"

#include <string.h>

#include "../../storage/fat32_types.h"
#include "files_image_config.h"
static uint8_t ppm_parse_header(const uint8_t *data, uint16_t len, uint16_t *width, uint16_t *height, uint32_t *data_offset)
{
    uint32_t values[3] = {0};
    uint8_t value_index = 0;
    uint8_t in_number = 0;

    if(len < 8 || data[0] != 'P' || data[1] != '6')
        return 0;

    for(uint16_t i = 2; i < len; i++)
    {
        uint8_t c = data[i];
        if(c >= '0' && c <= '9')
        {
            in_number = 1;
            values[value_index] = values[value_index] * 10U + (uint32_t)(c - '0');
            if(values[value_index] > 65535U)
                return 0;
        }
        else if(c == ' ' || c == '\n' || c == '\r' || c == '\t')
        {
            if(in_number)
            {
                value_index++;
                in_number = 0;
                if(value_index == 3)
                {
                    *width = (uint16_t)values[0];
                    *height = (uint16_t)values[1];
                    *data_offset = (uint32_t)i + 1U;
                    return values[2] == 255U && *width != 0 && *height != 0;
                }
            }
        }
        else
        {
            return 0;
        }
    }

    return 0;
}

file_result_t files_open_ppm(const fat_file_entry_t *entry, const file_image_sink_t *sink)
{
    uint8_t header[PHOTO_PPM_HEADER_MAX];
    fat_stream_t stream;
    uint16_t width;
    uint16_t height;
    uint32_t data_offset;
    uint32_t row_bytes;
    uint8_t *row = image_decode_row_buffer();
    uint32_t row_capacity = image_decode_row_buffer_size();

    memset(&stream, 0, sizeof(stream));
    if(!fat_file_read_at(entry, 0, header, sizeof(header)))
        return FILE_RESULT_READ_ERROR;
    if(!ppm_parse_header(header, sizeof(header), &width, &height, &data_offset) ||
       width > PHOTO_VIEW_MAX_W || height > PHOTO_VIEW_MAX_H)
        return FILE_RESULT_UNSUPPORTED_FORMAT;

    row_bytes = (uint32_t)width * 3U;
    if(row_bytes > row_capacity)
        return FILE_RESULT_TOO_LARGE;
    if(entry->size != data_offset + row_bytes * height)
        return FILE_RESULT_UNSUPPORTED_FORMAT;
    if(!fat_stream_open(&stream, entry, data_offset))
        return FILE_RESULT_READ_ERROR;

    if(!sink || !sink->render_row_span)
        return FILE_RESULT_NOT_FOUND;
    if(sink->begin)
        sink->begin(sink->context);
    for(uint32_t src_y = 0; src_y < height; src_y++)
    {
        if(!fat_stream_read(&stream, row, row_bytes))
            return FILE_RESULT_READ_ERROR;
        sink->render_row_span(sink->context, src_y, height, row, width, 24, 0);
    }

    return FILE_RESULT_OK;
}
