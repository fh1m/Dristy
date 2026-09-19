#ifndef HK_DISPLAY_STAGE_PRIVATE_H
#define HK_DISPLAY_STAGE_PRIVATE_H

#include <hackylens/capability/display.h>

/* Private MicroPython RPC transaction bridge.  This is deliberately outside
 * the public capability ABI; ordinary consumers use abort/retry. */
hk_result_t hk_display_stage_checkpoint(
    hk_owner_t owner, const hk_display_t *handle,
    uint16_t *commands, uint16_t *text_bytes);
hk_result_t hk_display_stage_restore(
    hk_owner_t owner, const hk_display_t *handle,
    uint16_t commands, uint16_t text_bytes);
hk_result_t hk_display_stage_keep_last_clear(
    hk_owner_t owner, const hk_display_t *handle);

#endif
