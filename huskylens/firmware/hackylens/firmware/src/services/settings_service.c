#include "../core/hk_binary.h"

#include <string.h>

#include "settings_snapshot.h"

#include "../config/settings_config.h"
#include "settings_service.h"
#include "settings_app_data.h"
#include "hk_config.h"
#if HK_ENABLE_CAMERA_FEATURE
#include "camera_persist_settings.h"
#endif

static uint8_t g_led_enabled = 0;
static uint8_t g_led_brightness = 20;
static uint8_t g_rgb_enabled = 0;
static uint8_t g_rgb_red = 20;
static uint8_t g_rgb_green = 20;
static uint8_t g_rgb_blue = 20;
static uint8_t g_screen_brightness = 90;
static uint8_t g_auto_sleep_minutes = 1;
static uint8_t g_feature_flags;
static external_link_uart_speed_t g_external_link_uart_speed = EXTERNAL_LINK_UART_SPEED_115200;
static uint8_t g_qr_decode_rate = SETTINGS_QR_DECODE_RATE_DEFAULT;
static hk_autostart_id_t g_autostart_id = HK_AUTOSTART_OFF;
static uint8_t g_app_data[SETTINGS_APP_DATA_SIZE];

void settings_app_data_read(uint8_t data[SETTINGS_APP_DATA_SIZE])
{
    if(data)
        memcpy(data, g_app_data, sizeof(g_app_data));
}

void settings_app_data_write(const uint8_t data[SETTINGS_APP_DATA_SIZE])
{
    if(data)
        memcpy(g_app_data, data, sizeof(g_app_data));
}

uint8_t hk_auto_sleep_minutes(void)
{
    return g_auto_sleep_minutes;
}

uint8_t settings_led_enabled(void)
{
    return g_led_enabled;
}

uint8_t settings_led_brightness(void)
{
    return g_led_brightness;
}

uint8_t settings_rgb_enabled(void)
{
    return g_rgb_enabled;
}

uint8_t settings_rgb_red(void)
{
    return g_rgb_red;
}

uint8_t settings_rgb_green(void)
{
    return g_rgb_green;
}

uint8_t settings_rgb_blue(void)
{
    return g_rgb_blue;
}

uint8_t settings_screen_brightness(void)
{
    return g_screen_brightness;
}

uint8_t settings_auto_sleep_minutes(void)
{
    return g_auto_sleep_minutes;
}

uint8_t settings_feature_flags(void)
{
    return g_feature_flags;
}

void settings_set_led_enabled(uint8_t enabled)
{
    g_led_enabled = enabled ? 1 : 0;
}

void settings_set_led_brightness(uint8_t brightness)
{
    g_led_brightness = clamp_u8(brightness, 0, 100);
}

void settings_set_rgb_enabled(uint8_t enabled)
{
    g_rgb_enabled = enabled ? 1 : 0;
}

void settings_set_rgb_red(uint8_t red)
{
    g_rgb_red = clamp_u8(red, 0, 100);
}

void settings_set_rgb_green(uint8_t green)
{
    g_rgb_green = clamp_u8(green, 0, 100);
}

void settings_set_rgb_blue(uint8_t blue)
{
    g_rgb_blue = clamp_u8(blue, 0, 100);
}

void settings_set_screen_brightness(uint8_t brightness)
{
    g_screen_brightness = clamp_u8(brightness, 10, 100);
}

void settings_set_auto_sleep_minutes(uint8_t minutes)
{
    g_auto_sleep_minutes = clamp_u8(minutes, 1, 30);
}

void settings_set_feature_flags(uint8_t flags)
{
    g_feature_flags = flags & SETTINGS_FEATURE_FLAGS_MASK;
}

external_link_transport_t settings_external_link_transport(void)
{
    return (g_feature_flags & SETTINGS_EXTERNAL_LINK_I2C_FLAG) ? EXTERNAL_LINK_I2C : EXTERNAL_LINK_UART;
}

void settings_set_external_link_transport(external_link_transport_t transport)
{
    if(transport == EXTERNAL_LINK_I2C)
        g_feature_flags |= SETTINGS_EXTERNAL_LINK_I2C_FLAG;
    else
        g_feature_flags &= (uint8_t)~SETTINGS_EXTERNAL_LINK_I2C_FLAG;
}

external_link_uart_speed_t settings_external_link_uart_speed(void)
{
    return g_external_link_uart_speed;
}

uint32_t settings_external_link_uart_baud(void)
{
    if(g_external_link_uart_speed == EXTERNAL_LINK_UART_SPEED_9600)
        return 9600U;
    if(g_external_link_uart_speed == EXTERNAL_LINK_UART_SPEED_1000000)
        return 1000000U;
    return 115200U;
}

void settings_set_external_link_uart_speed(external_link_uart_speed_t speed)
{
    g_external_link_uart_speed = speed < EXTERNAL_LINK_UART_SPEED_COUNT ?
                                 speed : EXTERNAL_LINK_UART_SPEED_115200;
}

uint8_t settings_qr_decode_rate(void)
{
    return g_qr_decode_rate;
}

void settings_set_qr_decode_rate(uint8_t rate)
{
    g_qr_decode_rate = clamp_u8(rate,
                                SETTINGS_QR_DECODE_RATE_MIN,
                                SETTINGS_QR_DECODE_RATE_MAX);
}

hk_autostart_id_t settings_autostart_id(void)
{
    return g_autostart_id;
}

void settings_set_autostart_id(hk_autostart_id_t id)
{
    g_autostart_id = id >= HK_AUTOSTART_OFF && id < HK_AUTOSTART_COUNT ?
                     id : HK_AUTOSTART_OFF;
}

void settings_defaults(void)
{
    g_led_enabled = 0;
    g_led_brightness = 20;
    g_rgb_enabled = 0;
    g_rgb_red = 20;
    g_rgb_green = 20;
    g_rgb_blue = 20;
    g_screen_brightness = 90;
    g_auto_sleep_minutes = 1;
    g_feature_flags = 0;
    g_external_link_uart_speed = EXTERNAL_LINK_UART_SPEED_115200;
    g_qr_decode_rate = SETTINGS_QR_DECODE_RATE_DEFAULT;
    g_autostart_id = HK_AUTOSTART_OFF;
    memset(g_app_data, 0, sizeof(g_app_data));
#if HK_ENABLE_CAMERA_FEATURE
    camera_service_persist_defaults();
#endif
}

void settings_snapshot_capture(settings_snapshot_t *snapshot)
{
    if(!snapshot)
        return;

    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->led_enabled = g_led_enabled ? 1 : 0;
    snapshot->led_brightness = clamp_u8(g_led_brightness, 0, 100);
    snapshot->rgb_enabled = g_rgb_enabled ? 1 : 0;
    snapshot->rgb_red = clamp_u8(g_rgb_red, 0, 100);
    snapshot->rgb_green = clamp_u8(g_rgb_green, 0, 100);
    snapshot->rgb_blue = clamp_u8(g_rgb_blue, 0, 100);
    snapshot->screen_brightness = clamp_u8(g_screen_brightness, 10, 100);
    snapshot->auto_sleep_minutes = clamp_u8(g_auto_sleep_minutes, 1, 30);
    snapshot->feature_flags = g_feature_flags & SETTINGS_FEATURE_FLAGS_MASK;
    snapshot->external_link_uart_speed = (uint8_t)g_external_link_uart_speed;
    snapshot->autostart_id = (uint8_t)g_autostart_id;
    memcpy(snapshot->app_data, g_app_data, sizeof(snapshot->app_data));
#if HK_ENABLE_CAMERA_FEATURE
    camera_service_persist_get(&snapshot->camera);
#endif
    snapshot->qr_decode_rate = g_qr_decode_rate;
}

void settings_snapshot_apply(const settings_snapshot_t *snapshot)
{
    if(!snapshot)
        return;

    settings_set_led_enabled(snapshot->led_enabled);
    settings_set_led_brightness(snapshot->led_brightness);
    settings_set_rgb_enabled(snapshot->rgb_enabled);
    settings_set_rgb_red(snapshot->rgb_red);
    settings_set_rgb_green(snapshot->rgb_green);
    settings_set_rgb_blue(snapshot->rgb_blue);
    settings_set_screen_brightness(snapshot->screen_brightness);
    settings_set_auto_sleep_minutes(snapshot->auto_sleep_minutes);
    settings_set_feature_flags(snapshot->feature_flags);
    settings_set_external_link_uart_speed((external_link_uart_speed_t)snapshot->external_link_uart_speed);
    settings_set_autostart_id((hk_autostart_id_t)snapshot->autostart_id);
    memcpy(g_app_data, snapshot->app_data, sizeof(g_app_data));
#if HK_ENABLE_CAMERA_FEATURE
    camera_service_persist_apply(&snapshot->camera);
#endif
    settings_set_qr_decode_rate(snapshot->qr_decode_rate);
}
