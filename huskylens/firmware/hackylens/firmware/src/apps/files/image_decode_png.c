#include "image_decode.h"

#include <stddef.h>

#include <string.h>

#include "files_image_config.h"
#include "files_image_config.h"
#include "../../config/sd_config.h"
#include "image_decode_png_inflate.h"

#include "../../core/hk_binary.h"
#include "../../services/frame_workspace.h"

static uint8_t g_png_comp[PNG_COMP_MAX] __attribute__((aligned(4), section(".bss")));

static uint8_t png_paeth(uint8_t a, uint8_t b, uint8_t c)
{
    int16_t p = (int16_t)a + b - c;
    int16_t pa = p > a ? (int16_t)(p - a) : (int16_t)(a - p);
    int16_t pb = p > b ? (int16_t)(p - b) : (int16_t)(b - p);
    int16_t pc = p > c ? (int16_t)(p - c) : (int16_t)(c - p);
    if(pa <= pb && pa <= pc)
        return a;
    return pb <= pc ? b : c;
}

static uint8_t png_collect_idat(const fat_file_entry_t *entry, uint32_t *width, uint32_t *height, uint8_t *color_type, uint8_t *bpp, uint32_t *comp_size)
{
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    uint8_t buf[32];
    uint32_t offset = 8;
    uint32_t out = 0;
    uint8_t bit_depth;
    uint8_t compression;
    uint8_t filter;
    uint8_t interlace;
    uint8_t got_ihdr = 0;

    if(entry->size < 33 || !fat_file_read_at(entry, 0, buf, 8) || memcmp(buf, sig, sizeof(sig)) != 0)
        return 0;

    while(offset + 12U <= entry->size)
    {
        uint32_t len;
        char type[5];
        if(!fat_file_read_at(entry, offset, buf, 8))
            return 0;
        len = rd32be(buf);
        memcpy(type, &buf[4], 4);
        type[4] = '\0';
        offset += 8U;
        if(offset + len + 4U > entry->size)
            return 0;

        if(memcmp(type, "IHDR", 4) == 0)
        {
            if(len != 13 || !fat_file_read_at(entry, offset, buf, 13))
                return 0;
            *width = rd32be(buf);
            *height = rd32be(&buf[4]);
            bit_depth = buf[8];
            *color_type = buf[9];
            compression = buf[10];
            filter = buf[11];
            interlace = buf[12];
            if(bit_depth != 8 || compression != 0 || filter != 0 || interlace != 0 ||
               *width == 0 || *height == 0 || *width > PHOTO_VIEW_MAX_W || *height > PHOTO_VIEW_MAX_H)
                return 0;
            if(*color_type == 0)
                *bpp = 1;
            else if(*color_type == 2)
                *bpp = 3;
            else if(*color_type == 4)
                *bpp = 2;
            else if(*color_type == 6)
                *bpp = 4;
            else
                return 0;
            got_ihdr = 1;
        }
        else if(memcmp(type, "IDAT", 4) == 0)
        {
            if(!got_ihdr || out + len > PNG_COMP_MAX)
                return 0;
            uint32_t remain = len;
            uint32_t chunk_offset = offset;
            while(remain)
            {
                uint32_t take = remain > SD_BLOCK_SIZE ? SD_BLOCK_SIZE : remain;
                if(!fat_file_read_at(entry, chunk_offset, &g_png_comp[out], take))
                    return 0;
                chunk_offset += take;
                out += take;
                remain -= take;
            }
        }
        else if(memcmp(type, "IEND", 4) == 0)
        {
            *comp_size = out;
            return got_ihdr && out > 0;
        }

        offset += len + 4U;
    }

    return 0;
}

file_result_t files_open_png(const fat_file_entry_t *entry, const file_image_sink_t *sink)
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t comp_size = 0;
    uint32_t row_bytes;
    uint32_t out_size;
    uint8_t color_type = 0;
    uint8_t bpp = 0;
    uint8_t *png_out;
    uint8_t *photo_row = image_decode_row_buffer();
    file_result_t result = FILE_RESULT_OK;
    frame_workspace_borrow_t workspace = FRAME_WORKSPACE_BORROW_NONE;

    if(!png_collect_idat(entry, &width, &height, &color_type, &bpp, &comp_size))
        return FILE_RESULT_UNSUPPORTED_FORMAT;

    row_bytes = width * bpp;
    out_size = (row_bytes + 1U) * height;
    if(row_bytes > PHOTO_ROW_MAX_BYTES || out_size > PNG_OUT_MAX)
        return FILE_RESULT_TOO_LARGE;
    if(!frame_workspace_borrow(out_size, &workspace))
        return FILE_RESULT_TOO_LARGE;
    png_out = workspace.data;

    if(!png_zlib_inflate_to_buffer(g_png_comp, comp_size, png_out, out_size))
    {
        result = FILE_RESULT_UNSUPPORTED_FORMAT;
        goto cleanup;
    }

    if(!sink || !sink->render_row_span)
    {
        result = FILE_RESULT_NOT_FOUND;
        goto cleanup;
    }
    if(sink->begin)
        sink->begin(sink->context);
    for(uint32_t y = 0; y < height; y++)
    {
        uint8_t *row = &png_out[y * (row_bytes + 1U) + 1U];
        uint8_t *prev = y ? &png_out[(y - 1U) * (row_bytes + 1U) + 1U] : NULL;
        uint8_t filter = row[-1];

        for(uint32_t i = 0; i < row_bytes; i++)
        {
            uint8_t a = i >= bpp ? row[i - bpp] : 0;
            uint8_t b = prev ? prev[i] : 0;
            uint8_t c = (prev && i >= bpp) ? prev[i - bpp] : 0;
            if(filter == 1)
                row[i] = (uint8_t)(row[i] + a);
            else if(filter == 2)
                row[i] = (uint8_t)(row[i] + b);
            else if(filter == 3)
                row[i] = (uint8_t)(row[i] + (uint8_t)(((uint16_t)a + b) >> 1));
            else if(filter == 4)
                row[i] = (uint8_t)(row[i] + png_paeth(a, b, c));
            else if(filter != 0)
            {
                result = FILE_RESULT_UNSUPPORTED_FORMAT;
                goto cleanup;
            }
        }

        for(uint32_t x = 0; x < width; x++)
        {
            if(color_type == 0)
            {
                uint8_t g = row[x];
                photo_row[x * 3U + 0U] = g;
                photo_row[x * 3U + 1U] = g;
                photo_row[x * 3U + 2U] = g;
            }
            else if(color_type == 2)
            {
                photo_row[x * 3U + 0U] = row[x * 3U + 0U];
                photo_row[x * 3U + 1U] = row[x * 3U + 1U];
                photo_row[x * 3U + 2U] = row[x * 3U + 2U];
            }
            else if(color_type == 4)
            {
                uint8_t g = row[x * 2U];
                photo_row[x * 3U + 0U] = g;
                photo_row[x * 3U + 1U] = g;
                photo_row[x * 3U + 2U] = g;
            }
            else
            {
                photo_row[x * 3U + 0U] = row[x * 4U + 0U];
                photo_row[x * 3U + 1U] = row[x * 4U + 1U];
                photo_row[x * 3U + 2U] = row[x * 4U + 2U];
            }
        }
        sink->render_row_span(sink->context, y, height, photo_row, (uint16_t)width, 24, 0);
    }

cleanup:
    (void)frame_workspace_release(&workspace);
    return result;
}
