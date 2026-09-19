#ifndef SETTINGS_SERVICE_H
#define SETTINGS_SERVICE_H

#include <stdint.h>

#include "../core/hk_app.h"
#include "external_link_types.h"

void settings_defaults(void);
uint8_t settings_led_enabled(void);
uint8_t settings_led_brightness(void);
uint8_t settings_rgb_enabled(void);
uint8_t settings_rgb_red(void);
uint8_t settings_rgb_green(void);
uint8_t settings_rgb_blue(void);
uint8_t settings_screen_brightness(void);
uint8_t settings_auto_sleep_minutes(void);
uint8_t settings_feature_flags(void);
uint8_t hk_auto_sleep_minutes(void);
void settings_set_led_enabled(uint8_t enabled);
void settings_set_led_brightness(uint8_t brightness);
void settings_set_rgb_enabled(uint8_t enabled);
void settings_set_rgb_red(uint8_t red);
void settings_set_rgb_green(uint8_t green);
void settings_set_rgb_blue(uint8_t blue);
void settings_set_screen_brightness(uint8_t brightness);
void settings_set_auto_sleep_minutes(uint8_t minutes);
void settings_set_feature_flags(uint8_t flags);
external_link_transport_t settings_external_link_transport(void);
void settings_set_external_link_transport(external_link_transport_t transport);
external_link_uart_speed_t settings_external_link_uart_speed(void);
uint32_t settings_external_link_uart_baud(void);
void settings_set_external_link_uart_speed(external_link_uart_speed_t speed);
uint8_t settings_qr_decode_rate(void);
void settings_set_qr_decode_rate(uint8_t rate);
hk_autostart_id_t settings_autostart_id(void);
void settings_set_autostart_id(hk_autostart_id_t id);

#endif
