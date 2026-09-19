#include "hk_log.h"

#include <stdarg.h>
#include <stdio.h>

#include "../config/debug_config.h"

static hk_log_sink_t s_sink;

void shell_log_set_sink(hk_log_sink_t sink)
{
    s_sink = sink;
}

void shell_printf(const char *fmt, ...)
{
    char message[TERM_MESSAGE_MAX];
    va_list args;

    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    printf("%s", message);
    if(s_sink)
        s_sink(message);
}
