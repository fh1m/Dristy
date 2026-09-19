#include "hal_gpio.h"

#include <gpiohs.h>

void hal_gpiohs_config_output(uint8_t pin)
{
    gpiohs_set_drive_mode(pin, GPIO_DM_OUTPUT);
}

void hal_gpiohs_config_input_pull_up(uint8_t pin)
{
    gpiohs_set_drive_mode(pin, GPIO_DM_INPUT_PULL_UP);
}

uint8_t hal_gpiohs_read(uint8_t pin)
{
    return gpiohs_get_pin(pin) ? 1 : 0;
}

void hal_gpiohs_write(uint8_t pin, uint8_t high)
{
    gpiohs_set_pin(pin, high ? GPIO_PV_HIGH : GPIO_PV_LOW);
}
