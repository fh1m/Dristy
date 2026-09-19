#ifndef HK_MICROPYTHON_RUNTIME_H
#define HK_MICROPYTHON_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#define MICROPYTHON_RUNTIME_SOURCE_MAX 65535U
#define MICROPYTHON_RUNTIME_HEAP_BYTES (128U * 1024U)
#define MICROPYTHON_RUNTIME_DEFAULT_LIMIT_MS 30000U
#define MICROPYTHON_RUNTIME_MAX_LIMIT_MS 300000U

typedef enum
{
    MICROPYTHON_RUNTIME_STOPPED = 0,
    MICROPYTHON_RUNTIME_STARTING,
    MICROPYTHON_RUNTIME_RUNNING,
    MICROPYTHON_RUNTIME_STOPPING,
    MICROPYTHON_RUNTIME_FINISHED,
    MICROPYTHON_RUNTIME_ERROR,
} micropython_runtime_state_t;

typedef enum
{
    MICROPYTHON_EXIT_NONE = 0,
    MICROPYTHON_EXIT_COMPLETE,
    MICROPYTHON_EXIT_REQUESTED,
    MICROPYTHON_EXIT_TIMEOUT,
    MICROPYTHON_EXIT_EXCEPTION,
    MICROPYTHON_EXIT_BUSY,
    MICROPYTHON_EXIT_INVALID_SOURCE,
    MICROPYTHON_EXIT_CORE1_FAILURE,
} micropython_runtime_exit_t;

typedef struct
{
    micropython_runtime_state_t state;
    micropython_runtime_exit_t exit_reason;
    uint32_t run_id;
    uint32_t source_bytes;
    uint32_t output_pending;
    uint32_t output_dropped;
    uint64_t started_us;
    uint64_t heartbeat_us;
    uint64_t deadline_us;
} micropython_runtime_status_t;

uint8_t micropython_runtime_start(const char *source, size_t length,
                                  uint32_t time_limit_ms);
uint8_t micropython_runtime_request_stop(void);
void micropython_runtime_poll(void);
void micropython_runtime_get_status(micropython_runtime_status_t *status);
size_t micropython_runtime_read_output(char *destination, size_t capacity);
/* Independent cursor API for UI, protocol events and reconnecting readers. */
size_t micropython_runtime_read_output_since(uint32_t *cursor,
                                             char *destination,
                                             size_t capacity,
                                             uint32_t *lost_bytes);

/* Port hooks called only from the MicroPython VM running on core 1. */
/* Non-raising phase used by cancellable bindings before they release their
 * request context.  A true result remains latched until vm_hook raises. */
uint8_t micropython_runtime_interrupt_pending(void);
void micropython_runtime_vm_hook(void);
void micropython_runtime_gc_hook(void);
void micropython_runtime_stdout_write(const char *data, size_t length);

#endif
