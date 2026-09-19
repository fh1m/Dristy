#include "screenshot_source.h"

#include <stddef.h>

#include "../drivers/lcd_st7789_transport.h"

static uint16_t screenshot_display_pixel(void *context, uint16_t x, uint16_t y)
{
    (void)context;
    return lcd_st7789_transport_shadow_pixel(x, y);
}

static const screenshot_pixel_source_t g_lcd_shadow_source = {
    .pixel_at = screenshot_display_pixel,
    .context = NULL,
};

const screenshot_pixel_source_t *screenshot_source_lcd_shadow(void)
{
    return &g_lcd_shadow_source;
}
