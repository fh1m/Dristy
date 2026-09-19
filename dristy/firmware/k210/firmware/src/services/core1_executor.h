#ifndef HK_CORE1_EXECUTOR_H
#define HK_CORE1_EXECUTOR_H

#include <stdint.h>

typedef void (*core1_executor_job_fn)(void *context);

/*
 * Core 1 is a single shared resource on K210.  The executor starts it once
 * and accepts one non-blocking job at a time; feature modules retain
 * ownership of their job context and result buffers.
 */
uint8_t core1_executor_init(void);
uint8_t core1_executor_idle(void);
uint32_t core1_executor_submit(core1_executor_job_fn job, void *context);
uint8_t core1_executor_complete(uint32_t ticket);
uint8_t core1_executor_wait(uint32_t ticket, uint64_t timeout_us);

#endif
