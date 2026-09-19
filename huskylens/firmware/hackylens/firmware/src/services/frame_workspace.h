#ifndef HK_FRAME_WORKSPACE_H
#define HK_FRAME_WORKSPACE_H

#include <stdint.h>

typedef struct
{
    uint8_t *data;
    uint32_t size;
    uint32_t generation;
} frame_workspace_borrow_t;

#define FRAME_WORKSPACE_BORROW_NONE ((frame_workspace_borrow_t){0})

/* One fixed-capacity scratch borrow may exist at a time. The returned storage
 * remains valid until the matching token is released. A copied/stale token
 * cannot release a later borrow. Borrowing fails while camera slots are
 * reserved, and camera reservation fails while a workspace is borrowed. Once
 * generation UINT32_MAX has been issued, later borrows fail until reboot. */
uint8_t frame_workspace_borrow(
    uint32_t minimum_size, frame_workspace_borrow_t *borrow);
uint8_t frame_workspace_release(frame_workspace_borrow_t *borrow);

#if defined(HK_FRAME_POOL_TESTING)
/* Deterministic host-fixture hook; absent from production objects. */
uint8_t frame_workspace_test_set_generation(uint32_t generation);
#endif

#endif
