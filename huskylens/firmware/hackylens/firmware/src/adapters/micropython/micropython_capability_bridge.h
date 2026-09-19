#ifndef HK_MICROPYTHON_CAPABILITY_BRIDGE_H
#define HK_MICROPYTHON_CAPABILITY_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#define MICROPYTHON_BINDING_DATA_MAX 256U

typedef enum
{
    MICROPYTHON_BINDING_OP_BUTTONS = 1,
    MICROPYTHON_BINDING_OP_DISPLAY_CLEAR,
    MICROPYTHON_BINDING_OP_DISPLAY_TEXT,
    MICROPYTHON_BINDING_OP_DISPLAY_RECT,
    MICROPYTHON_BINDING_OP_DISPLAY_PRESENT,
    MICROPYTHON_BINDING_OP_LED,
    MICROPYTHON_BINDING_OP_RGB,
    MICROPYTHON_BINDING_OP_UART_INIT,
    MICROPYTHON_BINDING_OP_UART_WRITE,
    MICROPYTHON_BINDING_OP_UART_READ,
    MICROPYTHON_BINDING_OP_I2C_WRITE,
    MICROPYTHON_BINDING_OP_I2C_READ,
} micropython_binding_op_t;

typedef enum
{
    MICROPYTHON_BINDING_OK = 0,
    MICROPYTHON_BINDING_ERROR_INVALID_ARGUMENT,
    MICROPYTHON_BINDING_ERROR_NOT_ACTIVE,
    MICROPYTHON_BINDING_ERROR_BUSY,
    MICROPYTHON_BINDING_ERROR_TIMEOUT,
    MICROPYTHON_BINDING_ERROR_IO,
    MICROPYTHON_BINDING_ERROR_LIMIT,
} micropython_binding_result_t;

/* Core-0 lifecycle and dispatcher. */
void micropython_capability_bridge_prepare(uint32_t run_id);
void micropython_capability_bridge_tick(void);
void micropython_capability_bridge_cleanup(void);

/* Core-1 synchronous RPC. VM stop/deadline hooks remain live while waiting. */
micropython_binding_result_t micropython_binding_call(
    micropython_binding_op_t operation, const uint32_t arguments[6],
    const uint8_t *input, size_t input_length,
    uint8_t *output, size_t output_capacity, size_t *output_length);

const char *micropython_binding_result_name(
    micropython_binding_result_t result);

#if defined(MICROPYTHON_BINDING_TESTING)
uint8_t micropython_capability_bridge_test_cancel_acknowledged(void);
uint8_t micropython_capability_bridge_test_request_pending(void);
#endif

#endif
