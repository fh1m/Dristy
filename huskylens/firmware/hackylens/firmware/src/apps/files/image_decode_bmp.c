#include "image_decode.h"

#include <string.h>

#include "../../storage/fat32_types.h"
#include "files_image_config.h"
#include "files_image_config.h"
#include "../../core/hk_binary.h"

file_result_t files_open_bmp(const fat_file_entry_t *entry, const file_image_sink_t *sink)
{
    uint8_t header[PHOTO_BMP565_HEADER_SIZE];
    uint32_t data_offset;
    uint32_t dib_size;
    uint32_t width;
    uint32_t height;
    uint16_t bpp;
    uint32_t compression;
    uint32_t row_bytes;
    uint8_t *row = image_decode_row_buffer();
    uint32_t row_capacity = image_decode_row_buffer_size();
    fat_stream_t stream;

    memset(&stream, 0, sizeof(stream));

    if(!sink || !sink->render_row_span)
        return FILE_RESULT_NOT_FOUND;
    if(!fat_file_read_at(entry, 0, header, sizeof(header)))
        return FILE_RESULT_READ_ERROR;

    data_offset = rd32(&header[10]);
    dib_size = rd32(&header[14]);
    width = rd32(&header[18]);
    height = rd32(&header[22]);
    bpp = rd16(&header[28]);
    compression = rd32(&header[30]);

    if(header[0] != 'B' || header[1] != 'M' || dib_size < 40 ||
       width == 0 || height == 0 || width > PHOTO_VIEW_MAX_W || height > PHOTO_VIEW_MAX_H)
        return FILE_RESULT_UNSUPPORTED_FORMAT;

    if(bpp == 24 && compression == 0)
        row_bytes = ((width * 3U) + 3U) & ~3U;
    else if(bpp == 16 && compression == 3 &&
            rd32(&header[54]) == 0x0000F800UL &&
            rd32(&header[58]) == 0x000007E0UL &&
            rd32(&header[62]) == 0x0000001FUL)
        row_bytes = ((width * 2U) + 3U) & ~3U;
    else
        return FILE_RESULT_UNSUPPORTED_FORMAT;

    if(!fat_stream_open(&stream, entry, data_offset))
        return FILE_RESULT_READ_ERROR;

    if(row_bytes > row_capacity)
        return FILE_RESULT_TOO_LARGE;

    if(sink->begin)
        sink->begin(sink->context);
    for(uint32_t file_row = 0; file_row < height; file_row++)
    {
        if(!fat_stream_read(&stream, row, row_bytes))
            return FILE_RESULT_READ_ERROR;
        uint32_t src_y = height - 1U - file_row;
        if(bpp == 24)
            sink->render_row_span(sink->context, src_y, height, row, (uint16_t)width, 24, 1);
        else
            sink->render_row_span(sink->context, src_y, height, row, (uint16_t)width, 16, 0);
    }

    return FILE_RESULT_OK;
}
