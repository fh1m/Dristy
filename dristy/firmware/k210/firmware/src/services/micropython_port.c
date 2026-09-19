#include <stddef.h>

#include "micropython_runtime.h"
#include "py/mphal.h"

void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len)
{
    micropython_runtime_stdout_write(str, len);
}
