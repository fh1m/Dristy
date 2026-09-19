#ifndef HAL_CORE_H
#define HAL_CORE_H

typedef int (*hal_core_entry_t)(void *context);

int hal_core1_start(hal_core_entry_t entry, void *context);

#endif
