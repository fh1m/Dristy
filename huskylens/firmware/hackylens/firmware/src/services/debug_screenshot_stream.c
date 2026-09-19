#include "debug_screenshot_stream.h"

#include <stdio.h>
#include "../config/display_config.h"
#include "../config/sd_config.h"
#include "../core/hk_binary.h"
#include "../storage/screenshot_bmp.h"
#include "debug_console_service.h"

static uint8_t g_screenshot_stream_buffer[SD_BLOCK_SIZE];

void debug_uart_send_screenshot(const screenshot_pixel_source_t *source)
{
    uint32_t file_size = screenshot_bmp_file_size();
    uint32_t offset = 0;
    uint32_t crc = 0;
    char header[80];
    char footer[40];

    snprintf(header, sizeof(header), "HKSHOT BEGIN BMP24 %u %u %u\n", HK_DISPLAY_REQUIRED_WIDTH, HK_DISPLAY_REQUIRED_HEIGHT, (unsigned)file_size);
    debug_console_write_text(header);

    while(offset < file_size)
    {
        uint16_t take = (uint16_t)((file_size - offset) > SD_BLOCK_SIZE ? SD_BLOCK_SIZE : (file_size - offset));
        screenshot_bmp_fill_bytes(source, offset, g_screenshot_stream_buffer, take);
        crc = crc32_update(crc, g_screenshot_stream_buffer, take);
        debug_console_write(g_screenshot_stream_buffer, take);
        offset += take;
    }

    snprintf(footer, sizeof(footer), "\nHKSHOT END %08X\n", (unsigned)crc);
    debug_console_write_text(footer);
}
