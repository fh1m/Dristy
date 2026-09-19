#ifndef HAL_TIME_H
#define HAL_TIME_H

#include <stdint.h>

uint64_t hal_time_us(void);
void hal_sleep_ms(uint32_t ms);

#endif
