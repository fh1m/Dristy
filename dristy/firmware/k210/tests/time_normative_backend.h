#ifndef HK_TIME_NORMATIVE_BACKEND_H
#define HK_TIME_NORMATIVE_BACKEND_H

#include <stdint.h>

#include "../firmware/src/capabilities/capability_provider.h"

const hk_capability_provider_t *time_normative_backend_provider(void);
const char *time_normative_backend_name(void);
uint64_t time_normative_backend_reset(void);
void time_normative_backend_set_now(uint64_t now_us);
void time_normative_backend_set_freeze(uint8_t freeze);
uint32_t time_normative_backend_sleep_calls(void);
uint64_t time_normative_backend_slept_us(void);

#endif
