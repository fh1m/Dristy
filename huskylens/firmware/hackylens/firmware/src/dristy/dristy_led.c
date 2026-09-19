#include "dristy_led.h"
#include "dristy_pipeline.h"
#include "dristy_result_bus.h"

#include "../../drivers/hk_lights.h"
#include "hal_time.h"

static dristy_led_mode_t g_mode = DRISTY_LED_STATUS;
static uint8_t g_brightness = 50;
static dristy_led_color_t g_custom_color = {0, 0, 255};
static uint32_t g_tick_count;
static uint8_t g_last_target_valid;

/* Scale colour by brightness percentage */
static void apply_led(dristy_led_color_t c)
{
    uint8_t r = (uint8_t)((uint16_t)c.r * g_brightness / 100);
    uint8_t g = (uint8_t)((uint16_t)c.g * g_brightness / 100);
    uint8_t b = (uint8_t)((uint16_t)c.b * g_brightness / 100);
    lights_rgb_set(1U, r, g, b);
}

static dristy_led_color_t mode_color(dristy_mode_t mode)
{
    switch(mode)
    {
    case DRISTY_MODE_DETECT:
    case DRISTY_MODE_DETECT_CUSTOM:
    case DRISTY_MODE_DETECT_TRACK:
        return DRISTY_LED_BLUE;
    case DRISTY_MODE_CLASSIFY:
        return DRISTY_LED_MAGENTA;
    case DRISTY_MODE_FACE_DETECT:
    case DRISTY_MODE_FACE_RECOGNISE:
        return DRISTY_LED_CYAN;
    case DRISTY_MODE_COLOUR_TRACK:
    case DRISTY_MODE_COLOUR_SORT:
        return DRISTY_LED_YELLOW;
    case DRISTY_MODE_APRILTAG:
    case DRISTY_MODE_DETECT_TAG:
        return DRISTY_LED_YELLOW;
    case DRISTY_MODE_QR_CODE:
        return DRISTY_LED_WHITE;
    case DRISTY_MODE_OPTICAL_FLOW:
        return DRISTY_LED_CYAN;
    case DRISTY_MODE_LINE_FOLLOW:
        return DRISTY_LED_GREEN;
    case DRISTY_MODE_LANDING_TARGET:
        return DRISTY_LED_GREEN;
    default:
        return DRISTY_LED_WHITE;
    }
}

void dristy_led_init(void)
{
    g_tick_count = 0;
    g_last_target_valid = 0;
    lights_rgb_set(0U, 0U, 0U, 0U);
}

void dristy_led_set_mode(dristy_led_mode_t mode)
{
    g_mode = mode;
}

void dristy_led_set_brightness(uint8_t percent)
{
    g_brightness = percent > 100 ? 100 : percent;
}

void dristy_led_set_color(dristy_led_color_t color)
{
    g_custom_color = color;
}

void dristy_led_tick(void)
{
    g_tick_count++;

    const dristy_pipeline_state_t *state = dristy_pipeline_state();
    const dristy_result_snapshot_t *snap = dristy_result_bus_read();
    uint8_t target_valid = (snap && snap->target.type != DRISTY_TARGET_NONE);

    switch(g_mode)
    {
    case DRISTY_LED_OFF:
        lights_rgb_set(0U, 0U, 0U, 0U);
        break;

    case DRISTY_LED_STATUS:
    {
        /* Slow breathe: 2-second cycle */
        dristy_led_color_t c = state ? mode_color(state->mode) : DRISTY_LED_WHITE;
        uint8_t phase = (uint8_t)((g_tick_count % 60) * 255 / 60);
        /* Triangle wave for breathing */
        uint8_t intensity = phase < 128 ? phase * 2 : (255 - phase) * 2;
        c.r = (uint8_t)((uint16_t)c.r * intensity / 255);
        c.g = (uint8_t)((uint16_t)c.g * intensity / 255);
        c.b = (uint8_t)((uint16_t)c.b * intensity / 255);
        apply_led(c);
        break;
    }

    case DRISTY_LED_DETECTION:
    {
        /* Flash on new detection */
        if(snap && snap->detection_count > 0 && (g_tick_count % 6) < 3)
            apply_led(DRISTY_LED_GREEN);
        else
            lights_rgb_set(0U, 0U, 0U, 0U);
        break;
    }

    case DRISTY_LED_TRACKING:
    {
        if(target_valid)
        {
            /* Solid green when target locked */
            apply_led(DRISTY_LED_GREEN);
        }
        else
        {
            /* Slow blink blue when searching */
            if((g_tick_count % 30) < 15)
                apply_led(DRISTY_LED_BLUE);
            else
                lights_rgb_set(0U, 0U, 0U, 0U);
        }
        break;
    }

    case DRISTY_LED_LANDING:
    {
        if(target_valid)
        {
            /* Blink rate proportional to distance (closer = faster) */
            uint16_t range = snap ? (uint16_t)snap->target.range_mm : 0;
            uint8_t period;
            if(range > 0 && range < 500)
                period = 4; /* very close: fast blink */
            else if(range > 0 && range < 2000)
                period = 10;
            else
                period = 20; /* far or unknown: slow blink */

            if((g_tick_count % period) < period / 2)
                apply_led(DRISTY_LED_GREEN);
            else
                lights_rgb_set(0U, 0U, 0U, 0U);
        }
        else
        {
            /* Red blink = no landing target */
            if((g_tick_count % 10) < 5)
                apply_led(DRISTY_LED_RED);
            else
                lights_rgb_set(0U, 0U, 0U, 0U);
        }
        break;
    }

    case DRISTY_LED_ERROR:
    {
        /* Rapid red flash */
        if((g_tick_count % 6) < 3)
            apply_led(DRISTY_LED_RED);
        else
            lights_rgb_set(0U, 0U, 0U, 0U);
        break;
    }

    case DRISTY_LED_CUSTOM:
        apply_led(g_custom_color);
        break;
    }

    g_last_target_valid = target_valid;
}

void dristy_led_set_illumination(uint8_t percent)
{
    lights_illum_set(percent > 0U ? 1U : 0U, percent > 100U ? 100U : percent);
}
