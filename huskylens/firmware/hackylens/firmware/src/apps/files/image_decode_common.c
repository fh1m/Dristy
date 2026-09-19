#include "image_decode.h"

#include "files_image_config.h"

static uint8_t g_photo_row[PHOTO_ROW_MAX_BYTES];

uint8_t *image_decode_row_buffer(void)
{
    return g_photo_row;
}

uint32_t image_decode_row_buffer_size(void)
{
    return sizeof(g_photo_row);
}
