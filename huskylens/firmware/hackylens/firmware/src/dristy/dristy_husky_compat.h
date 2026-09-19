#ifndef DRISTY_HUSKY_COMPAT_H
#define DRISTY_HUSKY_COMPAT_H

#include <stdint.h>

/* Stock HuskyLens commands (0x20–0x3E) on 0x55/0xAA framing.
 * Returns 1 if handled and response_cmd/response_len filled. */
int dristy_husky_compat_handle(uint8_t command,
                               const uint8_t *data, uint8_t data_len,
                               uint8_t *response_cmd,
                               uint8_t *response_buf, uint8_t *response_len);

#endif
