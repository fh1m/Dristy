#ifndef HK_TEST_INPUT_NORMATIVE_BACKEND_H
#define HK_TEST_INPUT_NORMATIVE_BACKEND_H

#include "../firmware/src/capabilities/capability_provider.h"

const hk_capability_provider_t *input_normative_backend_provider(void);
const char *input_normative_backend_name(void);
void input_normative_backend_reset(void);
hk_result_t input_normative_backend_sample(uint64_t timestamp_us,
                                           uint32_t raw_state);

#endif
