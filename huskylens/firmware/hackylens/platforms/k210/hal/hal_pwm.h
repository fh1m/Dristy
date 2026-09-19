#ifndef HAL_PWM_H
#define HAL_PWM_H

#include <stdint.h>

void hal_pwm_init(uint8_t device);
void hal_pwm_set(uint8_t device, uint8_t channel, double frequency_hz, double duty);
void hal_pwm_enable(uint8_t device, uint8_t channel, uint8_t enabled);

#endif
