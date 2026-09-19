#include "hal_core.h"

#include <entry.h>

int hal_core1_start(hal_core_entry_t entry, void *context)
{
    return register_core1(entry, context);
}
