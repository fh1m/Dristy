#ifndef HUSKY_ISP_SLIP_H
#define HUSKY_ISP_SLIP_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    SLIP_OUTSIDE,
    SLIP_FRAME,
    SLIP_ESCAPE,
} slip_state_t;

typedef struct {
    slip_state_t state;
    size_t length;
    uint8_t overflow;
} slip_decoder_t;

/* Returns 1 for a complete non-empty frame, 0 otherwise. */
int slip_decode_byte(slip_decoder_t *decoder, uint8_t byte,
                     uint8_t *output, size_t capacity);
void slip_send(const void *data, size_t length);

#endif
