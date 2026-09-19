#include "core1_executor.h"

#include <stddef.h>

#include "hal_core.h"
#include "hal_time.h"

#define CORE1_EXECUTOR_START_TIMEOUT_US 250000ULL
#define CORE1_EXECUTOR_IDLE_MS 1U

/* K210's two CPU cores are not cache coherent.  Both cores must access the
 * mailbox through the SRAM uncached alias (0x4...), never through its normal
 * cached address (0x8...). */
typedef struct
{
    volatile uint32_t started;
    volatile uint32_t online;
    volatile uint32_t start_failed;
    volatile uint32_t request_ticket;
    volatile uint32_t complete_ticket;
    core1_executor_job_fn volatile job;
    void *volatile context;
} core1_executor_control_t;

static core1_executor_control_t g_control_storage __attribute__((aligned(64)));
static uint8_t g_control_initialized;

static core1_executor_control_t *core1_executor_control(void)
{
    uintptr_t address = (uintptr_t)&g_control_storage;

    if(address >= 0x80000000UL && address < 0x80600000UL)
        address -= 0x40000000UL;
    return (core1_executor_control_t *)address;
}

static core1_executor_control_t *core1_executor_control_init(void)
{
    core1_executor_control_t *control = core1_executor_control();

    if(!g_control_initialized)
    {
        control->started = 0U;
        control->online = 0U;
        control->start_failed = 0U;
        control->request_ticket = 0U;
        control->complete_ticket = 0U;
        control->job = NULL;
        control->context = NULL;
        __sync_synchronize();
        g_control_initialized = 1U;
    }
    return control;
}

static int core1_executor_loop(void *context)
{
    core1_executor_control_t *control = core1_executor_control();

    (void)context;
    control->online = 1U;
    __sync_synchronize();

    while(1)
    {
        uint32_t ticket = control->request_ticket;

        if(ticket != control->complete_ticket)
        {
            core1_executor_job_fn job;
            void *job_context;

            __sync_synchronize();
            job = control->job;
            job_context = control->context;
            if(job)
                job(job_context);
            __sync_synchronize();
            control->complete_ticket = ticket;
            continue;
        }
        hal_sleep_ms(CORE1_EXECUTOR_IDLE_MS);
    }

    return 0;
}

uint8_t core1_executor_init(void)
{
    core1_executor_control_t *control = core1_executor_control_init();
    uint64_t deadline;

    if(control->online)
        return 1U;
    if(control->start_failed)
        return 0U;
    if(!control->started)
    {
        control->started = 1U;
        __sync_synchronize();
        if(hal_core1_start(core1_executor_loop, NULL) != 0)
        {
            control->start_failed = 1U;
            return 0U;
        }
    }

    deadline = hal_time_us() + CORE1_EXECUTOR_START_TIMEOUT_US;
    while(!control->online && hal_time_us() < deadline)
        hal_sleep_ms(1U);
    if(!control->online)
        control->start_failed = 1U;
    return control->online != 0U;
}

uint8_t core1_executor_idle(void)
{
    core1_executor_control_t *control = core1_executor_control_init();

    return control->online && control->request_ticket == control->complete_ticket;
}

uint32_t core1_executor_submit(core1_executor_job_fn job, void *context)
{
    core1_executor_control_t *control = core1_executor_control_init();
    uint32_t ticket;

    if(!job || !core1_executor_init() || !core1_executor_idle())
        return 0U;
    ticket = control->request_ticket + 1U;
    if(ticket == 0U)
        ticket = 1U;
    control->job = job;
    control->context = context;
    __sync_synchronize();
    control->request_ticket = ticket;
    return ticket;
}

uint8_t core1_executor_complete(uint32_t ticket)
{
    core1_executor_control_t *control = core1_executor_control_init();
    uint32_t completed;

    if(!ticket)
        return 0U;
    __sync_synchronize();
    completed = control->complete_ticket;
    __sync_synchronize();
    /*
     * A client may observe its completion after another feature has already
     * submitted and completed a newer job. Treat the monotonically increasing
     * ticket as a wrap-safe sequence, not as an edge that must be acknowledged
     * before the next submission.
     */
    return (int32_t)(completed - ticket) >= 0;
}

uint8_t core1_executor_wait(uint32_t ticket, uint64_t timeout_us)
{
    uint64_t deadline;

    if(!ticket)
        return 0U;
    deadline = hal_time_us() + timeout_us;
    while(!core1_executor_complete(ticket))
    {
        if(hal_time_us() >= deadline)
            return 0U;
        hal_sleep_ms(1U);
    }
    return 1U;
}
