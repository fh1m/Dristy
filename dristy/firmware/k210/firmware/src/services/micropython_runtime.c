#include "micropython_runtime.h"

#include <stddef.h>
#include <stdint.h>

#include "../core/hk_screen.h"
#include "core1_executor.h"
#include "../adapters/micropython/micropython_capability_bridge.h"
#include "hal_time.h"
#include "hal_watchdog.h"
#include "hk_config.h"

#include "port/micropython_embed.h"
#include "py/compile.h"
#include "py/gc.h"
#include "py/parse.h"
#include "py/runtime.h"
#include "py/stackctrl.h"

#define MICROPYTHON_OUTPUT_BYTES 4096U
#define MICROPYTHON_OUTPUT_MASK (MICROPYTHON_OUTPUT_BYTES - 1U)
#define MICROPYTHON_STACK_LIMIT_BYTES (24U * 1024U)
#define MICROPYTHON_WATCHDOG_TIMEOUT_MS 2000ULL
#define MICROPYTHON_WATCHDOG_DEADLINE_GRACE_US 5000000ULL
#define MICROPYTHON_WATCHDOG_STOP_GRACE_US 2000000ULL

_Static_assert((MICROPYTHON_OUTPUT_BYTES & MICROPYTHON_OUTPUT_MASK) == 0U,
               "MicroPython output ring must be a power of two");

typedef struct
{
    volatile uint32_t state;
    volatile uint32_t exit_reason;
    volatile uint32_t run_id;
    volatile uint32_t source_bytes;
    volatile uint32_t stop_requested;
    volatile uint32_t output_read;
    volatile uint32_t output_write;
    volatile uint32_t output_dropped;
    volatile uint64_t started_us;
    volatile uint64_t heartbeat_us;
    volatile uint64_t deadline_us;
    volatile uint64_t stop_requested_us;
    char source[MICROPYTHON_RUNTIME_SOURCE_MAX + 1U];
    char output[MICROPYTHON_OUTPUT_BYTES];
} micropython_shared_t;

static micropython_shared_t g_shared_storage __attribute__((aligned(64)));
static uint8_t g_heap[MICROPYTHON_RUNTIME_HEAP_BYTES] __attribute__((aligned(16)));
static uint8_t g_initialized;
static uint32_t g_ticket;

static micropython_shared_t *micropython_shared(void)
{
    uintptr_t address = (uintptr_t)&g_shared_storage;

    if(address >= 0x80000000UL && address < 0x80600000UL)
        address -= 0x40000000UL;
    return (micropython_shared_t *)address;
}

static micropython_shared_t *micropython_shared_init(void)
{
    micropython_shared_t *shared = micropython_shared();

    if(!g_initialized)
    {
        shared->state = MICROPYTHON_RUNTIME_STOPPED;
        shared->exit_reason = MICROPYTHON_EXIT_NONE;
        shared->run_id = 0U;
        shared->source_bytes = 0U;
        shared->stop_requested = 0U;
        shared->output_read = 0U;
        shared->output_write = 0U;
        shared->output_dropped = 0U;
        shared->started_us = 0U;
        shared->heartbeat_us = 0U;
        shared->deadline_us = 0U;
        shared->stop_requested_us = 0U;
        __sync_synchronize();
        g_initialized = 1U;
    }
    return shared;
}

static uint8_t micropython_state_active(uint32_t state)
{
    return state == MICROPYTHON_RUNTIME_STARTING ||
           state == MICROPYTHON_RUNTIME_RUNNING ||
           state == MICROPYTHON_RUNTIME_STOPPING;
}

static void micropython_watchdog_poll(micropython_shared_t *shared)
{
    uint64_t now;
    uint8_t expired = 0U;

    if(!micropython_state_active(shared->state))
        return;

    now = hal_time_us();
    if(shared->deadline_us &&
       now > shared->deadline_us + MICROPYTHON_WATCHDOG_DEADLINE_GRACE_US)
        expired = 1U;
    if(shared->stop_requested_us &&
       now > shared->stop_requested_us + MICROPYTHON_WATCHDOG_STOP_GRACE_US)
        expired = 1U;
    if(!expired)
        return;

    /* Re-read after the decision so a just-completed core-1 job wins the race. */
    __sync_synchronize();
    if(micropython_state_active(shared->state))
        hal_watchdog_force_reset(MICROPYTHON_WATCHDOG_TIMEOUT_MS);
}

static uint8_t micropython_exec_source(micropython_shared_t *shared)
{
    nlr_buf_t nlr;

    if(nlr_push(&nlr) == 0)
    {
        mp_lexer_t *lexer = mp_lexer_new_from_str_len(
            MP_QSTR__lt_stdin_gt_, shared->source,
            shared->source_bytes, 0U);
        qstr source_name = lexer->source_name;
        mp_parse_tree_t parse_tree = mp_parse(lexer, MP_PARSE_FILE_INPUT);
        mp_obj_t module = mp_compile(&parse_tree, source_name, true);

        mp_call_function_0(module);
        nlr_pop();
        return 1U;
    }

    if(shared->exit_reason == MICROPYTHON_EXIT_NONE)
        shared->exit_reason = MICROPYTHON_EXIT_EXCEPTION;
    mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
    return 0U;
}

static void micropython_worker(void *context)
{
    micropython_shared_t *shared = micropython_shared();
    int stack_anchor;

    (void)context;
    shared->started_us = hal_time_us();
    shared->heartbeat_us = shared->started_us;
    shared->state = MICROPYTHON_RUNTIME_RUNNING;
    __sync_synchronize();

    mp_embed_init(g_heap, sizeof(g_heap), &stack_anchor);
    mp_stack_set_limit(MICROPYTHON_STACK_LIMIT_BYTES);
    (void)micropython_exec_source(shared);
    mp_embed_deinit();

    __sync_synchronize();
    if(shared->exit_reason == MICROPYTHON_EXIT_NONE)
        shared->exit_reason = MICROPYTHON_EXIT_COMPLETE;
    if(shared->exit_reason == MICROPYTHON_EXIT_COMPLETE)
        shared->state = MICROPYTHON_RUNTIME_FINISHED;
    else if(shared->exit_reason == MICROPYTHON_EXIT_EXCEPTION)
        shared->state = MICROPYTHON_RUNTIME_ERROR;
    else
        shared->state = MICROPYTHON_RUNTIME_STOPPED;
    shared->heartbeat_us = hal_time_us();
    __sync_synchronize();
}

uint8_t micropython_runtime_start(const char *source, size_t length,
                                  uint32_t time_limit_ms)
{
    micropython_shared_t *shared = micropython_shared_init();
    uint64_t now;

    micropython_runtime_poll();
    if(!source || !length || length > MICROPYTHON_RUNTIME_SOURCE_MAX)
    {
        shared->exit_reason = MICROPYTHON_EXIT_INVALID_SOURCE;
        shared->state = MICROPYTHON_RUNTIME_ERROR;
        return 0U;
    }
    if(shared->state == MICROPYTHON_RUNTIME_STARTING ||
       shared->state == MICROPYTHON_RUNTIME_RUNNING ||
       shared->state == MICROPYTHON_RUNTIME_STOPPING)
    {
        shared->exit_reason = MICROPYTHON_EXIT_BUSY;
        return 0U;
    }
    /* The worker publishes its terminal state before core 1 publishes the
       executor completion ticket.  Do not overwrite g_ticket or reset the
       capability bridge during that bounded handoff window; the next poll
       performs cleanup first. */
    if(g_ticket)
    {
        shared->exit_reason = MICROPYTHON_EXIT_BUSY;
        return 0U;
    }
    if(!core1_executor_init())
    {
        shared->exit_reason = MICROPYTHON_EXIT_CORE1_FAILURE;
        shared->state = MICROPYTHON_RUNTIME_ERROR;
        return 0U;
    }
    if(!core1_executor_idle())
    {
        shared->exit_reason = MICROPYTHON_EXIT_BUSY;
        return 0U;
    }

    /* An external HMPY/debug RUN is user activity too. Wake the device before
       core 1 can issue display or light bindings against a sleeping panel. */
    hk_screen_request_wake();

    if(!time_limit_ms)
        time_limit_ms = MICROPYTHON_RUNTIME_DEFAULT_LIMIT_MS;
    if(time_limit_ms > MICROPYTHON_RUNTIME_MAX_LIMIT_MS)
        time_limit_ms = MICROPYTHON_RUNTIME_MAX_LIMIT_MS;

    for(size_t i = 0; i < length; i++)
        shared->source[i] = source[i];
    shared->source[length] = '\0';
    shared->source_bytes = (uint32_t)length;
    shared->stop_requested = 0U;
    shared->stop_requested_us = 0U;
    shared->output_read = 0U;
    shared->output_write = 0U;
    shared->output_dropped = 0U;
    shared->exit_reason = MICROPYTHON_EXIT_NONE;
    shared->run_id++;
    if(shared->run_id == 0U)
        shared->run_id = 1U;
    now = hal_time_us();
    shared->started_us = now;
    shared->heartbeat_us = now;
    shared->deadline_us = now + (uint64_t)time_limit_ms * 1000ULL;
    shared->state = MICROPYTHON_RUNTIME_STARTING;
    __sync_synchronize();

    micropython_capability_bridge_prepare(shared->run_id);

    g_ticket = core1_executor_submit(micropython_worker, NULL);
    if(!g_ticket)
    {
        micropython_capability_bridge_cleanup();
        shared->exit_reason = MICROPYTHON_EXIT_CORE1_FAILURE;
        shared->state = MICROPYTHON_RUNTIME_ERROR;
        return 0U;
    }
    return 1U;
}

uint8_t micropython_runtime_request_stop(void)
{
    micropython_shared_t *shared = micropython_shared_init();

    if(shared->state != MICROPYTHON_RUNTIME_STARTING &&
       shared->state != MICROPYTHON_RUNTIME_RUNNING &&
       shared->state != MICROPYTHON_RUNTIME_STOPPING)
        return 0U;
    shared->stop_requested = 1U;
    if(!shared->stop_requested_us)
        shared->stop_requested_us = hal_time_us();
    shared->state = MICROPYTHON_RUNTIME_STOPPING;
    __sync_synchronize();
    return 1U;
}

void micropython_runtime_poll(void)
{
    micropython_shared_t *shared = micropython_shared_init();

    /* The worker publishes a terminal state only after VM deinit and after
       its final capability access.  That state is therefore the safe cleanup
       handoff even when the executor completion-ticket store trails it by a
       few instructions on the other core. */
    if(g_ticket &&
       (core1_executor_complete(g_ticket) ||
        !micropython_state_active(shared->state)))
    {
        g_ticket = 0U;
        micropython_capability_bridge_cleanup();
        __sync_synchronize();
        if(shared->state == MICROPYTHON_RUNTIME_STARTING ||
           shared->state == MICROPYTHON_RUNTIME_RUNNING ||
           shared->state == MICROPYTHON_RUNTIME_STOPPING)
        {
            shared->state = MICROPYTHON_RUNTIME_ERROR;
            shared->exit_reason = MICROPYTHON_EXIT_CORE1_FAILURE;
        }
    }
    micropython_watchdog_poll(shared);
}

void micropython_runtime_get_status(micropython_runtime_status_t *status)
{
    micropython_shared_t *shared = micropython_shared_init();
    uint32_t write;
    uint32_t read;

    if(!status)
        return;
    __sync_synchronize();
    write = shared->output_write;
    read = shared->output_read;
    status->state = (micropython_runtime_state_t)shared->state;
    status->exit_reason = (micropython_runtime_exit_t)shared->exit_reason;
    status->run_id = shared->run_id;
    status->source_bytes = shared->source_bytes;
    status->output_pending = write - read > MICROPYTHON_OUTPUT_BYTES ?
                             MICROPYTHON_OUTPUT_BYTES : write - read;
    status->output_dropped = shared->output_dropped;
    status->started_us = shared->started_us;
    status->heartbeat_us = shared->heartbeat_us;
    status->deadline_us = shared->deadline_us;
}

size_t micropython_runtime_read_output(char *destination, size_t capacity)
{
    micropython_shared_t *shared = micropython_shared_init();
    uint32_t cursor = shared->output_read;
    size_t count = micropython_runtime_read_output_since(
        &cursor, destination, capacity, NULL);

    shared->output_read = cursor;
    return count;
}

size_t micropython_runtime_read_output_since(uint32_t *cursor,
                                             char *destination,
                                             size_t capacity,
                                             uint32_t *lost_bytes)
{
    micropython_shared_t *shared = micropython_shared_init();
    uint32_t read;
    uint32_t write;
    uint32_t oldest;
    size_t count;

    if(lost_bytes)
        *lost_bytes = 0U;
    if(!cursor || !destination || !capacity)
        return 0U;
    read = *cursor;
    __sync_synchronize();
    write = shared->output_write;
    oldest = write > MICROPYTHON_OUTPUT_BYTES ?
             write - MICROPYTHON_OUTPUT_BYTES : 0U;
    if((int32_t)(read - oldest) < 0)
    {
        if(lost_bytes)
            *lost_bytes = oldest - read;
        read = oldest;
    }
    if((int32_t)(write - read) < 0)
        read = write;
    count = (size_t)(write - read);
    if(count > capacity)
        count = capacity;
    for(size_t i = 0; i < count; i++)
        destination[i] = shared->output[(read + (uint32_t)i) & MICROPYTHON_OUTPUT_MASK];
    *cursor = read + (uint32_t)count;
    return count;
}

uint8_t micropython_runtime_interrupt_pending(void)
{
    micropython_shared_t *shared = micropython_shared();
    uint64_t now = hal_time_us();

    shared->heartbeat_us = now;
    if(shared->stop_requested)
    {
        shared->exit_reason = MICROPYTHON_EXIT_REQUESTED;
        return 1U;
    }
    if(shared->deadline_us && now >= shared->deadline_us)
    {
        shared->exit_reason = MICROPYTHON_EXIT_TIMEOUT;
        return 1U;
    }
    return 0U;
}

void micropython_runtime_vm_hook(void)
{
    if(micropython_runtime_interrupt_pending())
    {
#if HK_MICROPYTHON_WDT_FAULT_INJECTION
        /* Test-build-only deterministic fault injection.  Core 0 remains live
         * and must take the normal one-shot WDT1 fallback after its grace
         * period.  Production builds always compile this branch out. */
        while(1)
            __asm__ volatile("nop");
#else
        mp_raise_type(&mp_type_KeyboardInterrupt);
#endif
    }
}

void micropython_runtime_gc_hook(void)
{
    micropython_shared()->heartbeat_us = hal_time_us();
}

void micropython_runtime_stdout_write(const char *data, size_t length)
{
    micropython_shared_t *shared = micropython_shared();
    uint32_t write;

    if(!data || !length)
        return;
    write = shared->output_write;
    for(size_t i = 0; i < length; i++)
    {
        if(write - shared->output_read >= MICROPYTHON_OUTPUT_BYTES)
            shared->output_dropped++;
        shared->output[write & MICROPYTHON_OUTPUT_MASK] = data[i];
        write++;
    }
    __sync_synchronize();
    shared->output_write = write;
}
