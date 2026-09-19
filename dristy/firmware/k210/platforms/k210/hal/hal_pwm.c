#include "hal_pwm.h"

#include <pwm.h>

void hal_pwm_init(uint8_t device)
{
    pwm_init((pwm_device_number_t)device);
}

void hal_pwm_set(uint8_t device, uint8_t channel, double frequency_hz, double duty)
{
    pwm_set_frequency((pwm_device_number_t)device, (pwm_channel_number_t)channel, frequency_hz, duty);
}

void hal_pwm_enable(uint8_t device, uint8_t channel, uint8_t enabled)
{
    pwm_set_enable((pwm_device_number_t)device, (pwm_channel_number_t)channel, enabled ? 1 : 0);
}
