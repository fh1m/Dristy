#ifndef HAL_GPIO_H
#define HAL_GPIO_H

#include <stdint.h>

void hal_gpiohs_config_output(uint8_t pin);
void hal_gpiohs_config_input_pull_up(uint8_t pin);
uint8_t hal_gpiohs_read(uint8_t pin);
void hal_gpiohs_write(uint8_t pin, uint8_t high);

#endif
