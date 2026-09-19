#include "hk_lights.h"

#include "defaults.h"

#include "../core/hk_binary.h"
#include "hal_pwm.h"

void lights_driver_prepare(void)
{
    hal_pwm_init(LED_PWM_DEVICE);
}

void lights_screen_backlight_set(uint8_t percent)
{
    uint8_t duty = clamp_u8(percent, 10, 100);
    hal_pwm_set(SCREEN_BL_PWM_DEVICE, SCREEN_BL_PWM_CHANNEL, PWM_FREQ_HZ, (double)duty / 100.0);
    hal_pwm_enable(SCREEN_BL_PWM_DEVICE, SCREEN_BL_PWM_CHANNEL, 1);
}

void lights_screen_backlight_off(void)
{
    hal_pwm_enable(SCREEN_BL_PWM_DEVICE, SCREEN_BL_PWM_CHANNEL, 0);
}

void lights_illum_set(uint8_t enabled, uint8_t brightness)
{
    uint8_t duty = enabled ? clamp_u8(brightness, 0, 100) : 0;
    hal_pwm_set(LED_PWM_DEVICE, LED_PWM_CHANNEL, PWM_FREQ_HZ, (double)duty / 100.0);
    hal_pwm_enable(LED_PWM_DEVICE, LED_PWM_CHANNEL, enabled && duty > 0);
}

void lights_rgb_set(uint8_t enabled, uint8_t red, uint8_t green, uint8_t blue)
{
    uint8_t r = enabled ? clamp_u8(red, 0, 100) : 0;
    uint8_t g = enabled ? clamp_u8(green, 0, 100) : 0;
    uint8_t b = enabled ? clamp_u8(blue, 0, 100) : 0;

    hal_pwm_set(RGB_PWM_DEVICE, RGB_PWM_CHANNEL0, PWM_FREQ_HZ, (double)r / 100.0);
    hal_pwm_set(RGB_PWM_DEVICE, RGB_PWM_CHANNEL1, PWM_FREQ_HZ, (double)g / 100.0);
    hal_pwm_set(RGB_PWM_DEVICE, RGB_PWM_CHANNEL2, PWM_FREQ_HZ, (double)b / 100.0);
    hal_pwm_enable(RGB_PWM_DEVICE, RGB_PWM_CHANNEL0, enabled && r > 0);
    hal_pwm_enable(RGB_PWM_DEVICE, RGB_PWM_CHANNEL1, enabled && g > 0);
    hal_pwm_enable(RGB_PWM_DEVICE, RGB_PWM_CHANNEL2, enabled && b > 0);
}
