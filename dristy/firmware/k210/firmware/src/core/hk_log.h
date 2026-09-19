#ifndef HK_LOG_H
#define HK_LOG_H

typedef void (*hk_log_sink_t)(const char *message);

void shell_log_set_sink(hk_log_sink_t sink);
void shell_printf(const char *fmt, ...);

#endif
