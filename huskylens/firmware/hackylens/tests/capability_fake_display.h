#ifndef HK_CAPABILITY_FAKE_DISPLAY_H
#define HK_CAPABILITY_FAKE_DISPLAY_H

#include <hackylens/capability/display.h>

#define HK_FAKE_DISPLAY_WIDTH 16U
#define HK_FAKE_DISPLAY_HEIGHT 12U
#define HK_FAKE_DISPLAY_MAX_COMMANDS 8U
#define HK_FAKE_DISPLAY_MAX_TEXT_BYTES 32U
#define HK_FAKE_DISPLAY_MAX_DIRTY_RECTS 4U
#define HK_FAKE_DISPLAY_MAX_BORROWED_VIEWS 2U
#define HK_FAKE_DISPLAY_TRANSFER_SLICE_BYTES 8U
#define HK_FAKE_DISPLAY_MAX_PRESENT_US UINT32_C(500000)

typedef enum
{
    HK_FAKE_DISPLAY_COMMAND_CLEAR = 1,
    HK_FAKE_DISPLAY_COMMAND_FILL_RECT = 2,
    HK_FAKE_DISPLAY_COMMAND_STROKE_RECT = 3,
    HK_FAKE_DISPLAY_COMMAND_TEXT = 4,
    HK_FAKE_DISPLAY_COMMAND_BLIT = 5
} hk_fake_display_command_type_t;

typedef struct
{
    uint32_t type;
    hk_display_rect_t rect;
    uint16_t color;
    uint16_t reserved;
    uint32_t payload_bytes;
    uint32_t pixel_format;
    const void *borrowed_data;
    uint32_t source_x;
    uint32_t source_y;
    uint32_t source_stride_bytes;
} hk_fake_display_command_t;

typedef struct
{
    uint64_t transferred_bytes;
    uint64_t repair_bytes;
    uint64_t cleanup_bytes;
    uint32_t transferred_regions;
    uint32_t transfer_slices;
    uint32_t command_log_count;
    uint32_t active_planes;
    uint32_t staged_commands;
    uint32_t staged_text_bytes;
    uint32_t staged_dirty_rects;
    uint32_t borrowed_views;
    uint32_t committed_base_generation;
    uint32_t committed_overlay_generation;
    uint32_t needs_repair_planes;
    uint32_t quarantined;
    hk_deadline_t last_deadline;
    hk_result_t last_result;
} hk_fake_display_metrics_t;

void hk_fake_display_reset(uint32_t supported_planes);
void hk_fake_display_set_now_us(uint64_t now_us);
void hk_fake_display_set_slice_duration_us(uint32_t duration_us);
void hk_fake_display_fail_next_present(
    hk_result_t result, uint32_t after_transferred_slices);
const hk_fake_display_metrics_t *hk_fake_display_metrics(void);
const hk_fake_display_command_t *hk_fake_display_command(uint32_t index);
uint16_t hk_fake_display_panel_pixel(uint32_t plane, uint32_t x, uint32_t y);

#endif
