#ifndef HK_EXTERNAL_LINK_NORMATIVE_SUITE_H
#define HK_EXTERNAL_LINK_NORMATIVE_SUITE_H

#include <hackylens/capability/external_link.h>

typedef struct
{
    void (*reset)(void);
    void (*set_now_us)(uint64_t now_us);
    void (*set_i2c_rx)(const uint8_t *bytes, uint32_t size_bytes);
    void (*target_write)(const uint8_t *bytes, uint32_t size_bytes);
    void (*target_read)(uint8_t *bytes, uint32_t size_bytes);
    uint32_t (*uart_tx_bytes)(void);
} external_link_normative_backend_t;

int external_link_normative_suite_run(
    const external_link_normative_backend_t *backend);

#endif
