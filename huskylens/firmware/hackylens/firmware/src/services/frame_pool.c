#include "frame_pool.h"
#include "frame_workspace.h"

#include <stddef.h>

#include "../config/camera_config.h"

static uint16_t g_camera_slots[FRAME_POOL_CAMERA_SLOT_COUNT][CAMERA_MAX_FRAME_PIXELS]
    __attribute__((aligned(64), section(".bss")));
static uint32_t g_workspace_generation;
static uint32_t g_workspace_active_generation;
static uint8_t g_camera_reserved;

uint8_t frame_pool_camera_reserve(void)
{
    if(g_camera_reserved || g_workspace_active_generation != 0U)
        return 0U;
    g_camera_reserved = 1U;
    return 1U;
}

void frame_pool_camera_release(void)
{
    g_camera_reserved = 0U;
}

uint16_t *frame_pool_camera_slot(uint8_t index)
{
    if(!g_camera_reserved || index >= FRAME_POOL_CAMERA_SLOT_COUNT)
        return NULL;
    return g_camera_slots[index];
}

uint32_t frame_pool_camera_frame_bytes(void)
{
    return sizeof(g_camera_slots[0]);
}

uint8_t frame_workspace_borrow(
    uint32_t minimum_size, frame_workspace_borrow_t *borrow)
{
    uint32_t generation;

    if(!borrow || minimum_size == 0U ||
       minimum_size > sizeof(g_camera_slots[0]) || g_camera_reserved ||
       g_workspace_active_generation != 0U ||
       g_workspace_generation == UINT32_MAX)
        return 0U;
    generation = g_workspace_generation + 1U;
    g_workspace_generation = generation;
    g_workspace_active_generation = generation;
    *borrow = (frame_workspace_borrow_t){
        (uint8_t *)g_camera_slots[0], sizeof(g_camera_slots[0]), generation,
    };
    return 1U;
}

uint8_t frame_workspace_release(frame_workspace_borrow_t *borrow)
{
    uint8_t valid;

    if(!borrow)
        return 0U;
    valid = (uint8_t)(borrow->generation != 0U &&
                      borrow->generation == g_workspace_active_generation &&
                      borrow->data == (uint8_t *)g_camera_slots[0]);
    if(valid)
        g_workspace_active_generation = 0U;
    *borrow = FRAME_WORKSPACE_BORROW_NONE;
    return valid;
}

#if defined(HK_FRAME_POOL_TESTING)
uint8_t frame_workspace_test_set_generation(uint32_t generation)
{
    if(g_camera_reserved || g_workspace_active_generation != 0U)
        return 0U;
    g_workspace_generation = generation;
    return 1U;
}
#endif
