#include "camera_stream.h"

#include <stddef.h>

#include "../services/frame_pool.h"
#include "hal_dvp.h"

typedef enum
{
    CAMERA_SLOT_FREE = 0,
    CAMERA_SLOT_CAPTURING,
    CAMERA_SLOT_READY,
    CAMERA_SLOT_LEASED,
} camera_slot_state_t;

typedef struct
{
    volatile uint8_t state;
    volatile uint32_t sequence;
    volatile uint32_t lease_id;
} camera_slot_t;

static camera_slot_t g_slots[FRAME_POOL_CAMERA_SLOT_COUNT];
static volatile uint8_t g_running;
static volatile uint8_t g_paused;
static volatile uint8_t g_sync_pending;
static volatile int8_t g_capture_slot = -1;
static volatile uint32_t g_next_sequence;
static volatile uint32_t g_next_lease_id;
static volatile uint32_t g_irq_start_count;
static volatile uint32_t g_irq_finish_count;
static volatile uint32_t g_convert_count;
static volatile uint32_t g_captured_count;
static volatile uint32_t g_ready_drop_count;
static volatile uint32_t g_busy_drop_count;
static volatile uint32_t g_last_status;
static camera_stream_convert_start_hook_t g_convert_start_hook;
static camera_stream_convert_finish_hook_t g_convert_finish_hook;
static void *g_convert_hook_context;

static uint16_t *camera_stream_slot_uncached(uint8_t index)
{
    uintptr_t addr = (uintptr_t)frame_pool_camera_slot(index);
    if(addr >= 0x80000000UL && addr < 0x80600000UL)
        addr -= 0x40000000UL;
    return (uint16_t *)addr;
}

static int8_t camera_stream_capture_candidate(void)
{
    int8_t oldest_ready = -1;

    for(uint8_t i = 0; i < FRAME_POOL_CAMERA_SLOT_COUNT; i++)
    {
        if(g_slots[i].state == CAMERA_SLOT_FREE)
            return (int8_t)i;
        if(g_slots[i].state == CAMERA_SLOT_READY &&
           (oldest_ready < 0 || g_slots[i].sequence < g_slots[(uint8_t)oldest_ready].sequence))
            oldest_ready = (int8_t)i;
    }
    return oldest_ready;
}

static void camera_stream_on_event(hal_dvp_event_t event, uint32_t status, void *ctx)
{
    int8_t slot;
    (void)ctx;
    g_last_status = status;

    if(event == HAL_DVP_EVENT_FRAME_FINISH)
    {
        uint32_t sequence = 0U;

        slot = g_capture_slot;
        g_irq_finish_count++;
        if(slot >= 0 && g_slots[(uint8_t)slot].state == CAMERA_SLOT_CAPTURING)
        {
            __sync_synchronize();
            sequence = ++g_next_sequence;
            g_slots[(uint8_t)slot].sequence = sequence;
            g_slots[(uint8_t)slot].state = CAMERA_SLOT_READY;
            g_capture_slot = -1;
            g_captured_count++;
        }
        if(sequence && g_convert_finish_hook)
            g_convert_finish_hook(sequence, g_convert_hook_context);
        return;
    }

    g_irq_start_count++;
    if(!g_running || g_paused || g_capture_slot >= 0)
        return;
    if(g_sync_pending)
    {
        g_sync_pending = 0;
        return;
    }

    slot = camera_stream_capture_candidate();
    if(slot < 0)
    {
        g_busy_drop_count++;
        return;
    }
    if(g_slots[(uint8_t)slot].state == CAMERA_SLOT_READY)
        g_ready_drop_count++;
    g_slots[(uint8_t)slot].state = CAMERA_SLOT_CAPTURING;
    g_capture_slot = slot;
    __sync_synchronize();
    hal_dvp_set_display_addr((uint32_t)(uintptr_t)camera_stream_slot_uncached((uint8_t)slot));
    if(g_convert_start_hook)
        g_convert_start_hook(g_convert_hook_context);
    hal_dvp_start_convert();
    g_convert_count++;
}

uint8_t camera_stream_start(uint16_t width, uint16_t height, uint8_t burst)
{
    camera_stream_stop();
    if(!frame_pool_camera_reserve())
        return 0;
    for(uint8_t i = 0; i < FRAME_POOL_CAMERA_SLOT_COUNT; i++)
    {
        g_slots[i].state = CAMERA_SLOT_FREE;
        g_slots[i].sequence = 0;
        g_slots[i].lease_id = 0;
    }
    g_paused = 0;
    g_sync_pending = 1;
    g_capture_slot = -1;
    g_next_sequence = 0;
    g_next_lease_id = 0;
    g_irq_start_count = 0;
    g_irq_finish_count = 0;
    g_convert_count = 0;
    g_captured_count = 0;
    g_ready_drop_count = 0;
    g_busy_drop_count = 0;
    g_last_status = hal_dvp_status();
    hal_dvp_config_rgb565(width,
                          height,
                          (uint32_t)(uintptr_t)camera_stream_slot_uncached(0),
                          burst);
    g_running = 1;
    if(!hal_dvp_irq_start(camera_stream_on_event, NULL))
    {
        g_running = 0;
        frame_pool_camera_release();
        return 0;
    }
    return 1;
}

void camera_stream_stop(void)
{
    if(g_running)
    {
        hal_dvp_irq_stop();
        hal_dvp_stop_capture();
    }
    g_running = 0;
    g_paused = 1;
    g_sync_pending = 1;
    g_capture_slot = -1;
    for(uint8_t i = 0; i < FRAME_POOL_CAMERA_SLOT_COUNT; i++)
    {
        g_slots[i].state = CAMERA_SLOT_FREE;
        g_slots[i].lease_id = 0;
    }
    frame_pool_camera_release();
}

void camera_stream_pause(void)
{
    if(!g_running)
        return;
    hal_dvp_irq_mask();
    g_paused = 1;
    hal_dvp_irq_unmask();
}

void camera_stream_resume(void)
{
    if(!g_running)
        return;
    hal_dvp_irq_mask();
    if(g_running)
    {
        for(uint8_t i = 0; i < FRAME_POOL_CAMERA_SLOT_COUNT; i++)
        {
            if(g_slots[i].state == CAMERA_SLOT_READY)
                g_slots[i].state = CAMERA_SLOT_FREE;
        }
        g_paused = 0;
        g_sync_pending = 1;
        hal_dvp_clear_frame_start();
    }
    hal_dvp_irq_unmask();
}

uint8_t camera_stream_acquire_latest(camera_stream_frame_t *frame)
{
    int8_t newest = -1;
    uint32_t lease_id;

    if(!frame || !g_running)
        return 0;
    hal_dvp_irq_mask();
    for(uint8_t i = 0; i < FRAME_POOL_CAMERA_SLOT_COUNT; i++)
    {
        if(g_slots[i].state == CAMERA_SLOT_READY &&
           (newest < 0 || g_slots[i].sequence > g_slots[(uint8_t)newest].sequence))
            newest = (int8_t)i;
    }
    if(newest < 0)
    {
        hal_dvp_irq_unmask();
        return 0;
    }
    for(uint8_t i = 0; i < FRAME_POOL_CAMERA_SLOT_COUNT; i++)
    {
        if((int8_t)i != newest && g_slots[i].state == CAMERA_SLOT_READY)
        {
            g_slots[i].state = CAMERA_SLOT_FREE;
            g_ready_drop_count++;
        }
    }
    lease_id = ++g_next_lease_id;
    if(lease_id == 0)
        lease_id = ++g_next_lease_id;
    g_slots[(uint8_t)newest].lease_id = lease_id;
    g_slots[(uint8_t)newest].state = CAMERA_SLOT_LEASED;
    __sync_synchronize();
    frame->pixels = camera_stream_slot_uncached((uint8_t)newest);
    frame->lease_id = lease_id;
    frame->sequence = g_slots[(uint8_t)newest].sequence;
    hal_dvp_irq_unmask();
    return 1;
}

void camera_stream_release(uint32_t lease_id)
{
    if(lease_id == 0 || !g_running)
        return;
    hal_dvp_irq_mask();
    for(uint8_t i = 0; i < FRAME_POOL_CAMERA_SLOT_COUNT; i++)
    {
        if(g_slots[i].state == CAMERA_SLOT_LEASED && g_slots[i].lease_id == lease_id)
        {
            __sync_synchronize();
            g_slots[i].lease_id = 0;
            g_slots[i].state = CAMERA_SLOT_FREE;
            break;
        }
    }
    hal_dvp_irq_unmask();
}

uint8_t camera_stream_have_frame(void)
{
    uint8_t have = 0;
    if(!g_running)
        return 0;
    hal_dvp_irq_mask();
    for(uint8_t i = 0; i < FRAME_POOL_CAMERA_SLOT_COUNT; i++)
    {
        if(g_slots[i].state == CAMERA_SLOT_READY || g_slots[i].state == CAMERA_SLOT_LEASED)
        {
            have = 1;
            break;
        }
    }
    hal_dvp_irq_unmask();
    return have;
}

uint32_t camera_stream_frame_bytes(void)
{
    return frame_pool_camera_frame_bytes();
}

void camera_stream_status(camera_stream_status_t *status)
{
    if(!status)
        return;
    if(!g_running)
    {
        status->running = 0;
        status->paused = 1;
        status->have_frame = 0;
        status->state = CAMERA_STREAM_STOPPED;
        status->irq_start_count = g_irq_start_count;
        status->irq_finish_count = g_irq_finish_count;
        status->convert_count = g_convert_count;
        status->captured_count = g_captured_count;
        status->ready_drop_count = g_ready_drop_count;
        status->busy_drop_count = g_busy_drop_count;
        status->last_status = g_last_status;
        status->last_sequence = g_next_sequence;
        return;
    }
    hal_dvp_irq_mask();
    status->running = g_running;
    status->paused = g_paused;
    status->have_frame = 0;
    status->state = !g_running ? CAMERA_STREAM_STOPPED :
        (g_paused ? CAMERA_STREAM_PAUSED :
         (g_capture_slot >= 0 ? CAMERA_STREAM_CAPTURING :
          (g_sync_pending ? CAMERA_STREAM_SYNCING : CAMERA_STREAM_WAITING)));
    for(uint8_t i = 0; i < FRAME_POOL_CAMERA_SLOT_COUNT; i++)
    {
        if(g_slots[i].state == CAMERA_SLOT_READY || g_slots[i].state == CAMERA_SLOT_LEASED)
            status->have_frame = 1;
    }
    status->irq_start_count = g_irq_start_count;
    status->irq_finish_count = g_irq_finish_count;
    status->convert_count = g_convert_count;
    status->captured_count = g_captured_count;
    status->ready_drop_count = g_ready_drop_count;
    status->busy_drop_count = g_busy_drop_count;
    status->last_status = g_last_status;
    status->last_sequence = g_next_sequence;
    hal_dvp_irq_unmask();
}

void camera_stream_set_convert_hooks(
    camera_stream_convert_start_hook_t start,
    camera_stream_convert_finish_hook_t finish,
    void *context)
{
    if(g_running)
        hal_dvp_irq_mask();
    g_convert_start_hook = start;
    g_convert_finish_hook = finish;
    g_convert_hook_context = context;
    __sync_synchronize();
    if(g_running)
        hal_dvp_irq_unmask();
}

void camera_stream_clear_convert_hooks(void *context)
{
    if(g_running)
        hal_dvp_irq_mask();
    if(!context || context == g_convert_hook_context)
    {
        g_convert_start_hook = NULL;
        g_convert_finish_hook = NULL;
        g_convert_hook_context = NULL;
        __sync_synchronize();
    }
    if(g_running)
        hal_dvp_irq_unmask();
}
