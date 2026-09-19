#include "dristy_lcd.h"

#include "../../drivers/hk_lights.h"

static uint8_t g_lcd_state = 1U;
static uint8_t g_brightness = 80U;

void dristy_lcd_init(void)
{
    g_lcd_state = 1U;
    g_brightness = 80U;
    lights_screen_backlight_set(g_brightness);
}

void dristy_lcd_set_state(uint8_t state)
{
    g_lcd_state = state;
    if(state == 0U)
        lights_screen_backlight_off();
    else
        lights_screen_backlight_set(g_brightness);
}

void dristy_lcd_set_brightness(uint8_t percent)
{
    g_brightness = percent > 100U ? 100U : percent;
    if(g_lcd_state != 0U)
        lights_screen_backlight_set(g_brightness);
}

uint8_t dristy_lcd_state(void)
{
    return g_lcd_state;
}
