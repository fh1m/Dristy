#include "../../../firmware/src/capabilities/capability_provider.h"
#include "../../../firmware/src/capabilities/display_provider.h"
#include "../../../firmware/src/drivers/lcd_st7789_transport.h"
#include "../../../firmware/src/services/frame_workspace.h"
#include "../../../firmware/src/ui/hk_font.h"

#include <hackylens/capability/display.h>

#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "defaults.h"
#include "hackylens_font_1bpp.h"
#include "hal_time.h"

#define K210_DISPLAY_MAX_COMMANDS 32U
#define K210_DISPLAY_MAX_TEXT_BYTES 1024U
#define K210_DISPLAY_MAX_DIRTY_RECTS 8U
#define K210_DISPLAY_TRANSFER_SLICE_BYTES 128U
#define K210_DISPLAY_MAX_PRESENT_US 500000U

enum
{
    DISPLAY_STAGE_NONE = 0,
    DISPLAY_STAGE_BATCH = 1,
    DISPLAY_STAGE_SURFACE = 2,
};

enum
{
    DISPLAY_COMMAND_CLEAR = 1,
    DISPLAY_COMMAND_FILL,
    DISPLAY_COMMAND_STROKE,
    DISPLAY_COMMAND_TEXT,
    DISPLAY_COMMAND_BLIT,
};

typedef struct
{
    uint8_t type;
    uint8_t reserved;
    uint16_t color;
    uint16_t text_offset;
    uint16_t text_length;
    hk_display_rect_t rect;
} display_command_t;

typedef struct
{
    const uint8_t *data;
    uint32_t size_bytes;
    uint32_t stride_bytes;
    uint32_t source_x;
    uint32_t source_y;
    uint8_t active;
} display_borrow_t;

typedef struct
{
    hk_lease_t lease;
    hk_display_rect_t *repair;
    uint32_t committed_generation;
    uint16_t repair_count;
    uint8_t plane;
    uint8_t active;
} display_plane_t;

typedef struct
{
    hk_lease_t lease;
    hk_display_rect_t clip;
    hk_display_rect_t *dirty;
    display_command_t *commands;
    char *text;
    display_borrow_t borrow;
    uint16_t command_count;
    uint16_t text_bytes;
    uint16_t dirty_count;
    display_command_t rollback_first;
    uint8_t rollback_first_valid;
    uint8_t kind;
} display_stage_t;

typedef struct
{
    display_command_t *commands;
    char *text;
    hk_display_rect_t *dirty;
    uint16_t command_count;
    uint16_t text_bytes;
    uint16_t dirty_count;
} display_overlay_t;

typedef struct
{
    display_command_t stage_commands[K210_DISPLAY_MAX_COMMANDS];
    char stage_text[K210_DISPLAY_MAX_TEXT_BYTES];
    hk_display_rect_t stage_dirty[K210_DISPLAY_MAX_DIRTY_RECTS];
    display_command_t overlay_commands[K210_DISPLAY_MAX_COMMANDS];
    char overlay_text[K210_DISPLAY_MAX_TEXT_BYTES];
    hk_display_rect_t overlay_dirty[K210_DISPLAY_MAX_DIRTY_RECTS];
    hk_display_rect_t overlay_repair[K210_DISPLAY_MAX_DIRTY_RECTS];
} display_workspace_t;

typedef struct
{
    hk_lease_t lease;
    hk_display_rect_t dirty[K210_DISPLAY_MAX_DIRTY_RECTS];
    uint16_t dirty_count;
    uint8_t active;
} display_surface_stage_t;

typedef struct
{
    display_plane_t planes[2];
    display_stage_t stage;
    display_surface_stage_t surface;
    display_overlay_t overlay;
    hk_display_rect_t base_repair[K210_DISPLAY_MAX_DIRTY_RECTS];
    frame_workspace_borrow_t workspace_borrow;
    display_workspace_t *workspace;
} k210_display_state_t;

static k210_display_state_t s_display;

#if defined(K210_DISPLAY_ADAPTER_TESTING)
void hk_k210_display_test_reset(void)
{
    if(s_display.workspace_borrow.generation != 0U)
        (void)frame_workspace_release(&s_display.workspace_borrow);
    memset(&s_display, 0, sizeof(s_display));
}
#endif

static uint8_t owner_equal(hk_owner_t left, hk_owner_t right)
{
    return (uint8_t)(left.slot == right.slot &&
                     left.generation == right.generation);
}

static uint8_t lease_equal(const hk_lease_t *left, const hk_lease_t *right)
{
    return (uint8_t)(left && right && left->slot == right->slot &&
                     left->generation == right->generation &&
                     left->capability_id == right->capability_id &&
                     owner_equal(left->owner, right->owner));
}

static hk_display_rect_t screen_rect(void)
{
    hk_display_rect_t result = {0, 0, LCD_W, LCD_H};
    return result;
}

static uint8_t rect_empty(const hk_display_rect_t *rect)
{
    return (uint8_t)(rect->width == 0U || rect->height == 0U);
}

static hk_result_t rect_validate(const hk_display_rect_t *rect)
{
    if(!rect || (int64_t)rect->x + rect->width > INT32_MAX ||
       (int64_t)rect->y + rect->height > INT32_MAX)
        return HK_ERR_INVALID_ARGUMENT;
    return HK_OK;
}

static hk_display_rect_t rect_intersection(
    const hk_display_rect_t *left, const hk_display_rect_t *right)
{
    int64_t x = left->x > right->x ? left->x : right->x;
    int64_t y = left->y > right->y ? left->y : right->y;
    int64_t left_right = (int64_t)left->x + left->width;
    int64_t right_right = (int64_t)right->x + right->width;
    int64_t left_bottom = (int64_t)left->y + left->height;
    int64_t right_bottom = (int64_t)right->y + right->height;
    int64_t end_x = left_right < right_right ? left_right : right_right;
    int64_t end_y = left_bottom < right_bottom ? left_bottom : right_bottom;
    hk_display_rect_t result = {0, 0, 0U, 0U};

    if(end_x <= x || end_y <= y)
        return result;
    result.x = (int32_t)x;
    result.y = (int32_t)y;
    result.width = (uint32_t)(end_x - x);
    result.height = (uint32_t)(end_y - y);
    return result;
}

static uint8_t rect_overlaps(
    const hk_display_rect_t *left, const hk_display_rect_t *right)
{
    return (uint8_t)(
        left->x < (int64_t)right->x + right->width &&
        right->x < (int64_t)left->x + left->width &&
        left->y < (int64_t)right->y + right->height &&
        right->y < (int64_t)left->y + left->height);
}

static hk_display_rect_t rect_union(
    const hk_display_rect_t *left, const hk_display_rect_t *right)
{
    int32_t x = left->x < right->x ? left->x : right->x;
    int32_t y = left->y < right->y ? left->y : right->y;
    int64_t end_x_left = (int64_t)left->x + left->width;
    int64_t end_x_right = (int64_t)right->x + right->width;
    int64_t end_y_left = (int64_t)left->y + left->height;
    int64_t end_y_right = (int64_t)right->y + right->height;
    hk_display_rect_t result = {
        x, y,
        (uint32_t)((end_x_left > end_x_right ? end_x_left : end_x_right) - x),
        (uint32_t)((end_y_left > end_y_right ? end_y_left : end_y_right) - y),
    };
    return result;
}

static hk_result_t dirty_add(
    hk_display_rect_t *dirty, uint16_t *count,
    const hk_display_rect_t *rect)
{
    hk_display_rect_t candidate = *rect;

    if(rect_empty(rect))
        return HK_OK;
    for(uint16_t index = 0U; index < *count;)
    {
        if(!rect_overlaps(&candidate, &dirty[index]))
        {
            index++;
            continue;
        }
        candidate = rect_union(&candidate, &dirty[index]);
        (*count)--;
        dirty[index] = dirty[*count];
        index = 0U;
    }
    if(*count >= K210_DISPLAY_MAX_DIRTY_RECTS)
        return HK_ERR_LIMIT;
    dirty[(*count)++] = candidate;
    return HK_OK;
}

static hk_result_t dirty_preview(
    const display_stage_t *stage, const hk_display_rect_t *rect,
    hk_display_rect_t *dirty, uint16_t *count)
{
    memcpy(dirty, stage->dirty,
           sizeof(dirty[0]) * K210_DISPLAY_MAX_DIRTY_RECTS);
    *count = stage->dirty_count;
    return dirty_add(dirty, count, rect);
}

static display_plane_t *find_plane(
    k210_display_state_t *state, const hk_lease_t *lease)
{
    if(!state || !lease)
        return NULL;
    for(uint16_t index = 0U; index < 2U; index++)
    {
        if(state->planes[index].active &&
           lease_equal(&state->planes[index].lease, lease))
            return &state->planes[index];
    }
    return NULL;
}

static uint8_t workspace_acquire(k210_display_state_t *state)
{
    display_workspace_t *workspace;

    if(state->workspace)
        return 1U;
    if(!frame_workspace_borrow(
            sizeof(display_workspace_t), &state->workspace_borrow))
        return 0U;
    workspace = (display_workspace_t *)state->workspace_borrow.data;
    memset(workspace, 0, sizeof(*workspace));
    state->workspace = workspace;
    state->stage.commands = workspace->stage_commands;
    state->stage.text = workspace->stage_text;
    state->stage.dirty = workspace->stage_dirty;
    state->overlay.commands = workspace->overlay_commands;
    state->overlay.text = workspace->overlay_text;
    state->overlay.dirty = workspace->overlay_dirty;
    state->planes[1].repair = workspace->overlay_repair;
    return 1U;
}

static void workspace_release_if_idle(k210_display_state_t *state)
{
    if(!state->workspace || state->stage.kind != DISPLAY_STAGE_NONE ||
       state->overlay.command_count != 0U || state->planes[1].repair_count != 0U)
        return;
    (void)frame_workspace_release(&state->workspace_borrow);
    state->workspace = NULL;
    state->stage.commands = NULL;
    state->stage.text = NULL;
    state->stage.dirty = NULL;
    state->overlay.commands = NULL;
    state->overlay.text = NULL;
    state->overlay.dirty = NULL;
    state->planes[1].repair = NULL;
}

static void overlay_reset(display_overlay_t *overlay)
{
    overlay->command_count = 0U;
    overlay->text_bytes = 0U;
    overlay->dirty_count = 0U;
}

static void stage_reset(display_stage_t *stage)
{
    stage->lease = HK_LEASE_NONE;
    stage->clip = screen_rect();
    stage->command_count = 0U;
    stage->text_bytes = 0U;
    stage->dirty_count = 0U;
    stage->borrow = (display_borrow_t){0};
    stage->rollback_first_valid = 0U;
    stage->kind = DISPLAY_STAGE_NONE;
}

static hk_result_t stage_for(
    k210_display_state_t *state, const hk_lease_t *lease,
    uint8_t kind, display_plane_t **plane, display_stage_t **stage)
{
    display_plane_t *found = find_plane(state, lease);

    if(!found)
        return HK_ERR_INTERNAL;
    if(state->stage.kind != kind ||
       !lease_equal(&state->stage.lease, lease))
        return HK_ERR_INVALID_STATE;
    if(plane)
        *plane = found;
    if(stage)
        *stage = &state->stage;
    return HK_OK;
}

static hk_result_t k210_display_open(
    void *context, const hk_lease_t *lease, uint32_t plane)
{
    k210_display_state_t *state = (k210_display_state_t *)context;
    uint16_t index = plane == HK_DISPLAY_PLANE_BASE ? 0U : 1U;

    if(!state || !lease ||
       (plane != HK_DISPLAY_PLANE_BASE &&
        plane != HK_DISPLAY_PLANE_OVERLAY))
        return HK_ERR_INVALID_ARGUMENT;
    if(state->planes[index].active)
        return HK_ERR_BUSY;
    state->planes[index].lease = *lease;
    state->planes[index].plane = plane;
    state->planes[index].repair = index == 0U ? state->base_repair :
        (state->workspace ? state->workspace->overlay_repair : NULL);
    state->planes[index].repair_count = 0U;
    state->planes[index].active = 1U;
    return HK_OK;
}

static hk_result_t terminal_result(
    hk_deadline_t deadline, const hk_cancel_t *cancel)
{
    if(cancel && cancel->probe && cancel->probe(cancel->context))
        return HK_ERR_CANCELLED;
    if(deadline.at_us != 0U && hal_time_us() >= deadline.at_us)
        return HK_ERR_DEADLINE_EXCEEDED;
    return HK_OK;
}

static uint32_t utf8_next(
    const char *text, uint16_t end, uint16_t *position)
{
    uint8_t first;
    uint32_t codepoint;
    uint8_t required;

    if(!text || !position || *position >= end)
        return 0U;
    first = (uint8_t)text[(*position)++];
    if(first < 0x80U)
        return first;
    if((first & 0xE0U) == 0xC0U)
    {
        codepoint = first & 0x1FU;
        required = 1U;
    }
    else if((first & 0xF0U) == 0xE0U)
    {
        codepoint = first & 0x0FU;
        required = 2U;
    }
    else if((first & 0xF8U) == 0xF0U)
    {
        codepoint = first & 0x07U;
        required = 3U;
    }
    else
        return '?';
    if(required > end - *position)
    {
        *position = end;
        return '?';
    }
    for(uint8_t index = 0U; index < required; index++)
    {
        uint8_t next = (uint8_t)text[*position];
        if((next & 0xC0U) != 0x80U)
            return '?';
        (*position)++;
        codepoint = (codepoint << 6) | (next & 0x3FU);
    }
    return codepoint;
}

static void fill_span(
    uint8_t *pixels, uint16_t span_x, uint16_t span_width,
    uint16_t x0, uint16_t x1, uint16_t color)
{
    uint16_t start = x0 > span_x ? x0 : span_x;
    uint16_t span_end = (uint16_t)(span_x + span_width);
    uint16_t end = x1 < span_end ? x1 : span_end;

    for(uint16_t x = start; x < end; x++)
    {
        uint16_t offset = (uint16_t)((x - span_x) * 2U);
        pixels[offset] = (uint8_t)(color >> 8);
        pixels[offset + 1U] = (uint8_t)color;
    }
}

static void render_text_span(
    uint8_t *pixels, uint16_t span_x, uint16_t span_width, uint16_t y,
    const display_command_t *command, const char *text,
    uint16_t text_bytes)
{
    uint16_t position = command->text_offset;
    uint16_t end = (uint16_t)(position + command->text_length);
    uint32_t glyph_x = (uint32_t)command->rect.x;
    uint32_t glyph_row;

    if(end > text_bytes || y < (uint32_t)command->rect.y ||
       y >= (uint32_t)command->rect.y + command->rect.height)
        return;
    glyph_row = y - (uint32_t)command->rect.y;
    if(glyph_row >= HACKYLENS_FONT_H)
        return;
    while(position < end && glyph_x < LCD_W)
    {
        const uint8_t *glyph = hk_font_glyph(
            utf8_next(text, end, &position));

        for(uint16_t offset = 0U; offset < HACKYLENS_FONT_W; offset++)
        {
            uint32_t x = glyph_x + offset;
            uint8_t bit;

            if(x < (uint32_t)command->rect.x ||
               x >= (uint32_t)command->rect.x + command->rect.width ||
               x < span_x || x >= (uint32_t)span_x + span_width)
                continue;
            bit = glyph[glyph_row * HACKYLENS_FONT_ROW_BYTES + offset / 8U] &
                  (uint8_t)(0x80U >> (offset & 7U));
            if(bit)
                fill_span(pixels, span_x, span_width,
                          (uint16_t)x, (uint16_t)(x + 1U), command->color);
        }
        glyph_x += HACKYLENS_FONT_W;
    }
}

static void render_commands_span(
    uint8_t *pixels, uint16_t span_x, uint16_t span_width, uint16_t y,
    const display_command_t *commands, uint16_t command_count,
    const char *text, uint16_t text_bytes, const display_borrow_t *borrow)
{
    for(uint16_t index = 0U; index < command_count; index++)
    {
        const display_command_t *command = &commands[index];
        uint16_t x0 = (uint16_t)command->rect.x;
        uint16_t x1 = (uint16_t)(command->rect.x + command->rect.width);
        uint16_t y0 = (uint16_t)command->rect.y;
        uint16_t y1 = (uint16_t)(command->rect.y + command->rect.height);

        if(y < y0 || y >= y1)
            continue;
        if(command->type == DISPLAY_COMMAND_CLEAR ||
           command->type == DISPLAY_COMMAND_FILL)
            fill_span(pixels, span_x, span_width, x0, x1, command->color);
        else if(command->type == DISPLAY_COMMAND_STROKE)
        {
            if(y == y0 || y + 1U == y1)
                fill_span(pixels, span_x, span_width, x0, x1, command->color);
            else
            {
                fill_span(pixels, span_x, span_width, x0,
                          (uint16_t)(x0 + 1U), command->color);
                fill_span(pixels, span_x, span_width,
                          (uint16_t)(x1 - 1U), x1, command->color);
            }
        }
        else if(command->type == DISPLAY_COMMAND_TEXT)
            render_text_span(pixels, span_x, span_width, y, command,
                             text, text_bytes);
        else if(command->type == DISPLAY_COMMAND_BLIT && borrow && borrow->active)
        {
            uint16_t start = x0 > span_x ? x0 : span_x;
            uint16_t span_end = (uint16_t)(span_x + span_width);
            uint16_t end = x1 < span_end ? x1 : span_end;
            uint32_t source_y = borrow->source_y + y - y0;
            const uint8_t *source;

            if(start >= end)
                continue;
            source = borrow->data +
                source_y * borrow->stride_bytes +
                (uint32_t)(borrow->source_x + start - x0) * 2U;
            memcpy(pixels + (uint32_t)(start - span_x) * 2U,
                   source, (uint32_t)(end - start) * 2U);
        }
    }
}

static void render_span(
    uint8_t *pixels, uint16_t x, uint16_t width, uint16_t y,
    const display_stage_t *base_stage,
    const display_overlay_t *overlay)
{
    uint8_t *shadow = lcd_st7789_transport_shadow();

    memcpy(pixels, shadow + (uint32_t)y * LCD_W * 2U + (uint32_t)x * 2U,
           (uint32_t)width * 2U);
    if(base_stage)
        render_commands_span(
            pixels, x, width, y, base_stage->commands,
            base_stage->command_count, base_stage->text,
            base_stage->text_bytes, &base_stage->borrow);
    if(overlay)
        render_commands_span(
            pixels, x, width, y, overlay->commands,
            overlay->command_count, overlay->text,
            overlay->text_bytes, NULL);
}

static hk_result_t transfer_rect(
    const hk_display_rect_t *rect, const display_stage_t *base_stage,
    const display_overlay_t *overlay, hk_deadline_t deadline,
    const hk_cancel_t *cancel, uint8_t *progress)
{
    uint8_t pixels[K210_DISPLAY_TRANSFER_SLICE_BYTES];
    hk_result_t result = terminal_result(deadline, cancel);

    if(result != HK_OK)
        return result;
    result = lcd_st7789_transport_begin(rect, deadline);
    if(result != HK_OK)
        return result;
    for(uint32_t row = 0U; row < rect->height; row++)
    {
        uint16_t y = (uint16_t)((uint32_t)rect->y + row);
        uint32_t remaining = rect->width;
        uint16_t x = (uint16_t)rect->x;

        while(remaining > 0U)
        {
            uint16_t count = remaining * 2U > sizeof(pixels) ?
                (uint16_t)(sizeof(pixels) / 2U) : (uint16_t)remaining;
            result = terminal_result(deadline, cancel);
            if(result != HK_OK)
            {
                lcd_st7789_transport_abort();
                return result;
            }
            render_span(pixels, x, count, y, base_stage, overlay);
            result = lcd_st7789_transport_write(
                pixels, (size_t)count * 2U, deadline, cancel);
            if(result != HK_OK)
            {
                lcd_st7789_transport_abort();
                return result;
            }
            *progress = 1U;
            x = (uint16_t)(x + count);
            remaining -= count;
        }
    }
    result = lcd_st7789_transport_end(deadline, cancel);
    if(result != HK_OK)
        lcd_st7789_transport_abort();
    return result;
}

static hk_result_t transfer_regions(
    const hk_display_rect_t *regions, uint16_t count,
    const display_stage_t *base_stage, const display_overlay_t *overlay,
    hk_deadline_t deadline, const hk_cancel_t *cancel, uint8_t *progress)
{
    for(uint16_t index = 0U; index < count; index++)
    {
        hk_result_t result = transfer_rect(
            &regions[index], base_stage, overlay, deadline, cancel, progress);
        if(result != HK_OK)
            return result;
    }
    return HK_OK;
}

static void overlay_from_stage(
    display_overlay_t *overlay, const display_stage_t *stage)
{
    memcpy(overlay->commands, stage->commands,
           sizeof(stage->commands[0]) * stage->command_count);
    memcpy(overlay->text, stage->text, stage->text_bytes);
    memcpy(overlay->dirty, stage->dirty,
           sizeof(stage->dirty[0]) * stage->dirty_count);
    overlay->command_count = stage->command_count;
    overlay->text_bytes = stage->text_bytes;
    overlay->dirty_count = stage->dirty_count;
}

static void shadow_apply(const display_stage_t *stage)
{
    uint8_t pixels[K210_DISPLAY_TRANSFER_SLICE_BYTES];
    uint8_t *shadow = lcd_st7789_transport_shadow();

    for(uint16_t region = 0U; region < stage->dirty_count; region++)
    {
        const hk_display_rect_t *rect = &stage->dirty[region];
        for(uint32_t row = 0U; row < rect->height; row++)
        {
            uint16_t y = (uint16_t)((uint32_t)rect->y + row);
            uint32_t remaining = rect->width;
            uint16_t x = (uint16_t)rect->x;
            while(remaining > 0U)
            {
                uint16_t count = remaining * 2U > sizeof(pixels) ?
                    (uint16_t)(sizeof(pixels) / 2U) : (uint16_t)remaining;
                render_span(pixels, x, count, y, stage, NULL);
                memcpy(shadow + (uint32_t)y * LCD_W * 2U +
                           (uint32_t)x * 2U,
                       pixels, (uint32_t)count * 2U);
                x = (uint16_t)(x + count);
                remaining -= count;
            }
        }
    }
}

static hk_result_t k210_display_info(void *context, hk_display_info_t *info)
{
    (void)context;
    if(!info)
        return HK_ERR_INVALID_ARGUMENT;
    *info = (hk_display_info_t){
        sizeof(hk_display_info_t), HK_DISPLAY_INFO_VERSION,
        LCD_W, LCD_H, HK_DISPLAY_FORMAT_RGB565_BE, HK_DISPLAY_PLANE_ALL,
        4U, 2U, K210_DISPLAY_MAX_COMMANDS,
        K210_DISPLAY_MAX_TEXT_BYTES, K210_DISPLAY_MAX_DIRTY_RECTS, 1U,
        K210_DISPLAY_TRANSFER_SLICE_BYTES,
        K210_DISPLAY_MAX_PRESENT_US, 0U,
    };
    return HK_OK;
}

static hk_result_t k210_display_begin(
    void *context, const hk_lease_t *lease)
{
    k210_display_state_t *state = (k210_display_state_t *)context;

    if(!find_plane(state, lease))
        return HK_ERR_INTERNAL;
    if(state->surface.active &&
       lease_equal(&state->surface.lease, lease))
        return HK_ERR_INVALID_STATE;
    if(state->stage.kind != DISPLAY_STAGE_NONE)
        return HK_ERR_BUSY;
    if(!workspace_acquire(state))
        return HK_ERR_BUSY;
    stage_reset(&state->stage);
    state->stage.lease = *lease;
    state->stage.kind = DISPLAY_STAGE_BATCH;
    return HK_OK;
}

static hk_result_t k210_display_clip(
    void *context, const hk_lease_t *lease,
    const hk_display_rect_t *clip)
{
    k210_display_state_t *state = (k210_display_state_t *)context;
    display_stage_t *stage;
    hk_display_rect_t screen = screen_rect();
    hk_result_t result = stage_for(
        state, lease, DISPLAY_STAGE_BATCH, NULL, &stage);

    if(result != HK_OK)
        return result;
    if(!clip)
    {
        stage->clip = screen;
        return HK_OK;
    }
    result = rect_validate(clip);
    if(result != HK_OK)
        return result;
    stage->clip = rect_intersection(clip, &screen);
    return HK_OK;
}

static hk_result_t append_command(
    k210_display_state_t *state, const hk_lease_t *lease,
    display_command_t command, const char *text, uint16_t text_bytes,
    const display_borrow_t *borrow)
{
    display_stage_t *stage;
    hk_display_rect_t dirty[K210_DISPLAY_MAX_DIRTY_RECTS];
    uint16_t dirty_count;
    hk_result_t result = stage_for(
        state, lease, DISPLAY_STAGE_BATCH, NULL, &stage);

    if(result != HK_OK)
        return result;
    if(rect_empty(&command.rect))
        return HK_OK;
    if(stage->command_count >= K210_DISPLAY_MAX_COMMANDS ||
       text_bytes > K210_DISPLAY_MAX_TEXT_BYTES - stage->text_bytes ||
       (borrow && stage->borrow.active))
        return HK_ERR_LIMIT;
    result = dirty_preview(stage, &command.rect, dirty, &dirty_count);
    if(result != HK_OK)
        return result;
    if(text_bytes)
    {
        command.text_offset = stage->text_bytes;
        command.text_length = text_bytes;
        memcpy(stage->text + stage->text_bytes, text, text_bytes);
    }
    if(borrow)
        stage->borrow = *borrow;
    stage->commands[stage->command_count++] = command;
    stage->text_bytes = (uint16_t)(stage->text_bytes + text_bytes);
    memcpy(stage->dirty, dirty, sizeof(dirty));
    stage->dirty_count = dirty_count;
    return HK_OK;
}

static hk_result_t clipped_command(
    k210_display_state_t *state, const hk_lease_t *lease,
    const hk_display_rect_t *rect, uint8_t type, uint16_t color)
{
    display_stage_t *stage;
    display_command_t command = {0};
    hk_result_t result = stage_for(
        state, lease, DISPLAY_STAGE_BATCH, NULL, &stage);

    if(result != HK_OK)
        return result;
    result = rect_validate(rect);
    if(result != HK_OK)
        return result;
    command.type = type;
    command.color = color;
    command.rect = rect_intersection(rect, &stage->clip);
    return append_command(state, lease, command, NULL, 0U, NULL);
}

static hk_result_t k210_display_clear(
    void *context, const hk_lease_t *lease, uint16_t color)
{
    k210_display_state_t *state = (k210_display_state_t *)context;
    display_stage_t *stage;
    display_command_t command = {0};
    hk_result_t result = stage_for(
        state, lease, DISPLAY_STAGE_BATCH, NULL, &stage);

    if(result != HK_OK)
        return result;
    command.type = DISPLAY_COMMAND_CLEAR;
    command.color = color;
    command.rect = stage->clip;
    return append_command(state, lease, command, NULL, 0U, NULL);
}

static hk_result_t k210_display_fill(
    void *context, const hk_lease_t *lease,
    const hk_display_rect_t *rect, uint16_t color)
{
    return clipped_command(
        (k210_display_state_t *)context, lease, rect,
        DISPLAY_COMMAND_FILL, color);
}

static hk_result_t k210_display_stroke(
    void *context, const hk_lease_t *lease,
    const hk_display_rect_t *rect, uint16_t color)
{
    return clipped_command(
        (k210_display_state_t *)context, lease, rect,
        DISPLAY_COMMAND_STROKE, color);
}

static hk_result_t k210_display_text(
    void *context, const hk_lease_t *lease,
    const hk_display_rect_t *bounds, const char *utf8,
    uint32_t size_bytes, uint16_t color)
{
    k210_display_state_t *state = (k210_display_state_t *)context;
    display_stage_t *stage;
    display_command_t command = {0};
    hk_result_t result;

    if((size_bytes && !utf8) || size_bytes > UINT16_MAX)
        return HK_ERR_INVALID_ARGUMENT;
    result = stage_for(state, lease, DISPLAY_STAGE_BATCH, NULL, &stage);
    if(result != HK_OK)
        return result;
    result = rect_validate(bounds);
    if(result != HK_OK)
        return result;
    command.type = DISPLAY_COMMAND_TEXT;
    command.color = color;
    command.rect = rect_intersection(bounds, &stage->clip);
    return append_command(state, lease, command, utf8,
                          (uint16_t)size_bytes, NULL);
}

static hk_result_t k210_display_blit(
    void *context, const hk_lease_t *lease,
    const hk_display_rect_t *destination,
    const hk_buffer_view_t *pixels, uint32_t pixel_format)
{
    k210_display_state_t *state = (k210_display_state_t *)context;
    display_stage_t *stage;
    display_plane_t *plane;
    display_command_t command = {0};
    display_borrow_t borrow = {0};
    hk_result_t result;
    uint64_t required;

    if(!destination || !pixels ||
       pixel_format != HK_DISPLAY_FORMAT_RGB565_BE || !pixels->data)
        return HK_ERR_INVALID_ARGUMENT;
    result = rect_validate(destination);
    if(result != HK_OK)
        return result;
    if((uint64_t)destination->width * 2U > UINT32_MAX ||
       pixels->stride_bytes < (uint64_t)destination->width * 2U ||
       ((uintptr_t)pixels->data & 3U) != 0U ||
       (pixels->stride_bytes & 1U) != 0U)
        return HK_ERR_INVALID_ARGUMENT;
    result = stage_for(
        state, lease, DISPLAY_STAGE_BATCH, &plane, &stage);
    if(result != HK_OK)
        return result;
    if(plane->plane == HK_DISPLAY_PLANE_OVERLAY)
        return HK_ERR_FEATURE_UNAVAILABLE;
    required = destination->height == 0U ? 0U :
        (uint64_t)(destination->height - 1U) * pixels->stride_bytes +
        (uint64_t)destination->width * 2U;
    if(required > pixels->size_bytes)
        return HK_ERR_INVALID_ARGUMENT;
    command.type = DISPLAY_COMMAND_BLIT;
    command.rect = rect_intersection(destination, &stage->clip);
    borrow.data = (const uint8_t *)pixels->data;
    borrow.size_bytes = pixels->size_bytes;
    borrow.stride_bytes = pixels->stride_bytes;
    borrow.source_x = (uint32_t)(command.rect.x - destination->x);
    borrow.source_y = (uint32_t)(command.rect.y - destination->y);
    borrow.active = 1U;
    return append_command(state, lease, command, NULL, 0U, &borrow);
}

static hk_result_t k210_display_mark(
    void *context, const hk_lease_t *lease,
    const hk_display_rect_t *rect)
{
    k210_display_state_t *state = (k210_display_state_t *)context;
    display_stage_t *stage = NULL;
    display_surface_stage_t *surface = NULL;
    hk_display_rect_t screen = screen_rect();
    hk_display_rect_t clipped;
    hk_display_rect_t dirty[K210_DISPLAY_MAX_DIRTY_RECTS];
    uint16_t count;
    hk_result_t result;

    if(!find_plane(state, lease))
        return HK_ERR_INTERNAL;
    if(state->stage.kind != DISPLAY_STAGE_NONE &&
       lease_equal(&state->stage.lease, lease))
        stage = &state->stage;
    else if(state->surface.active &&
            lease_equal(&state->surface.lease, lease))
        surface = &state->surface;
    else
        return HK_ERR_INVALID_STATE;
    result = rect_validate(rect);
    if(result != HK_OK)
        return result;
    clipped = rect_intersection(rect, &screen);
    if(stage)
        result = dirty_preview(stage, &clipped, dirty, &count);
    else
    {
        memcpy(dirty, surface->dirty, sizeof(surface->dirty));
        count = surface->dirty_count;
        result = dirty_add(dirty, &count, &clipped);
    }
    if(result == HK_OK)
    {
        if(stage)
        {
            memcpy(stage->dirty, dirty, sizeof(dirty));
            stage->dirty_count = count;
        }
        else
        {
            memcpy(surface->dirty, dirty, sizeof(dirty));
            surface->dirty_count = count;
        }
    }
    return result;
}

static hk_result_t k210_display_surface(
    void *context, const hk_lease_t *lease, hk_display_surface_t *surface)
{
    k210_display_state_t *state = (k210_display_state_t *)context;
    display_plane_t *plane = find_plane(state, lease);

    if(!surface)
        return HK_ERR_INVALID_ARGUMENT;
    if(!plane)
        return HK_ERR_INTERNAL;
    if(plane->plane != HK_DISPLAY_PLANE_BASE)
        return HK_ERR_FEATURE_UNAVAILABLE;
    if(state->surface.active)
        return HK_ERR_BUSY;
    if(state->stage.kind != DISPLAY_STAGE_NONE &&
       lease_equal(&state->stage.lease, lease))
        return HK_ERR_INVALID_STATE;
    state->surface.lease = *lease;
    state->surface.dirty_count = 0U;
    state->surface.active = 1U;
    *surface = (hk_display_surface_t){
        sizeof(hk_display_surface_t), HK_DISPLAY_SURFACE_VERSION,
        {lcd_st7789_transport_shadow(),
         lcd_st7789_transport_shadow_size(),
         lcd_st7789_transport_stride(),
         HK_BUFFER_ACCESS_READABLE | HK_BUFFER_ACCESS_WRITABLE},
        LCD_W, LCD_H, HK_DISPLAY_FORMAT_RGB565_BE, 0U,
    };
    return HK_OK;
}

static hk_result_t repair_regions(
    k210_display_state_t *state, display_plane_t *plane,
    hk_deadline_t deadline, const hk_cancel_t *cancel, uint8_t *progress)
{
    const display_overlay_t *overlay = state->overlay.command_count ?
        &state->overlay : NULL;

    if(plane->repair_count == 0U)
        return HK_OK;
    /*
     * BASE surface borrows mutate the only shadow framebuffer in place.  A
     * repair therefore converges the panel to that authoritative backing
     * store; retained batches still pass their staged commands separately.
     */
    return transfer_regions(
        plane->repair, plane->repair_count, NULL, overlay,
        deadline, cancel, progress);
}

static hk_result_t k210_display_present(
    void *context, const hk_lease_t *lease,
    hk_deadline_t deadline, const hk_cancel_t *cancel)
{
    k210_display_state_t *state = (k210_display_state_t *)context;
    display_plane_t *plane;
    display_stage_t *stage = NULL;
    display_surface_stage_t *surface = NULL;
    display_overlay_t prospective;
    hk_display_rect_t affected[K210_DISPLAY_MAX_DIRTY_RECTS];
    uint16_t affected_count = 0U;
    uint8_t progress = 0U;
    hk_result_t result;

    if(deadline.at_us == UINT64_MAX)
        return HK_ERR_INVALID_ARGUMENT;
    if(!find_plane(state, lease))
        return HK_ERR_INTERNAL;
    if(state->stage.kind != DISPLAY_STAGE_NONE &&
       lease_equal(&state->stage.lease, lease))
        stage = &state->stage;
    else if(state->surface.active &&
            lease_equal(&state->surface.lease, lease))
        surface = &state->surface;
    else
        return HK_ERR_INVALID_STATE;
    plane = find_plane(state, lease);
    result = terminal_result(deadline, cancel);
    if(result != HK_OK)
        return result;
    result = repair_regions(state, plane, deadline, cancel, &progress);
    if(result != HK_OK)
        return result;
    plane->repair_count = 0U;

    for(uint16_t index = 0U;
        index < (stage ? stage->dirty_count : surface->dirty_count); index++)
    {
        const hk_display_rect_t *dirty = stage ?
            &stage->dirty[index] : &surface->dirty[index];
        result = dirty_add(affected, &affected_count, dirty);
        if(result != HK_OK)
            return result;
    }
    if(plane->plane == HK_DISPLAY_PLANE_OVERLAY)
    {
        for(uint16_t index = 0U; index < state->overlay.dirty_count; index++)
        {
            result = dirty_add(
                affected, &affected_count, &state->overlay.dirty[index]);
            if(result != HK_OK)
                return result;
        }
        prospective = (display_overlay_t){
            stage->commands, stage->text, stage->dirty,
            stage->command_count, stage->text_bytes, stage->dirty_count,
        };
        result = transfer_regions(
            affected, affected_count, NULL, &prospective,
            deadline, cancel, &progress);
    }
    else
    {
        const display_stage_t *base_stage = stage;
        const display_overlay_t *overlay = state->overlay.command_count ?
            &state->overlay : NULL;
        result = transfer_regions(
            affected, affected_count, base_stage, overlay,
            deadline, cancel, &progress);
    }
    if(result != HK_OK)
    {
        if(progress)
        {
            memcpy(plane->repair, affected,
                   sizeof(affected[0]) * affected_count);
            plane->repair_count = affected_count;
        }
        return result;
    }
    if(plane->plane == HK_DISPLAY_PLANE_OVERLAY)
        overlay_from_stage(&state->overlay, stage);
    else if(stage)
        shadow_apply(stage);
    if(plane->committed_generation != UINT32_MAX)
        plane->committed_generation++;
    if(stage)
    {
        stage_reset(stage);
        workspace_release_if_idle(state);
    }
    else
    {
        state->surface.lease = HK_LEASE_NONE;
        state->surface.dirty_count = 0U;
        state->surface.active = 0U;
    }
    return HK_OK;
}

static hk_result_t k210_display_abort(
    void *context, const hk_lease_t *lease)
{
    k210_display_state_t *state = (k210_display_state_t *)context;

    if(!find_plane(state, lease))
        return HK_ERR_INTERNAL;
    if(state->stage.kind != DISPLAY_STAGE_NONE &&
       lease_equal(&state->stage.lease, lease))
    {
        stage_reset(&state->stage);
        workspace_release_if_idle(state);
    }
    else if(state->surface.active &&
            lease_equal(&state->surface.lease, lease))
    {
        state->surface.lease = HK_LEASE_NONE;
        state->surface.dirty_count = 0U;
        state->surface.active = 0U;
    }
    else
        return HK_ERR_INVALID_STATE;
    return HK_OK;
}

static hk_result_t k210_display_checkpoint(
    void *context, const hk_lease_t *lease,
    uint16_t *commands, uint16_t *text_bytes)
{
    k210_display_state_t *state = (k210_display_state_t *)context;
    display_stage_t *stage;
    hk_result_t result = stage_for(
        state, lease, DISPLAY_STAGE_BATCH, NULL, &stage);

    if(result != HK_OK)
        return result;
    *commands = stage->command_count;
    *text_bytes = stage->text_bytes;
    stage->rollback_first_valid = 0U;
    return HK_OK;
}

static void dirty_rebuild(display_stage_t *stage)
{
    stage->dirty_count = 0U;
    for(uint16_t index = 0U; index < stage->command_count; index++)
        (void)dirty_add(stage->dirty, &stage->dirty_count,
                        &stage->commands[index].rect);
}

static hk_result_t k210_display_restore(
    void *context, const hk_lease_t *lease,
    uint16_t commands, uint16_t text_bytes)
{
    k210_display_state_t *state = (k210_display_state_t *)context;
    display_stage_t *stage;
    hk_result_t result = stage_for(
        state, lease, DISPLAY_STAGE_BATCH, NULL, &stage);

    if(result != HK_OK)
        return result;
    if(commands > K210_DISPLAY_MAX_COMMANDS ||
       text_bytes > K210_DISPLAY_MAX_TEXT_BYTES)
        return HK_ERR_INVALID_ARGUMENT;
    if(commands == 0U && stage->command_count != 0U)
    {
        stage->rollback_first = stage->commands[0];
        stage->rollback_first_valid = 1U;
    }
    if(stage->rollback_first_valid && commands != 0U)
        stage->commands[0] = stage->rollback_first;
    stage->command_count = commands;
    stage->text_bytes = text_bytes;
    if(commands == 0U)
        stage->borrow = (display_borrow_t){0};
    dirty_rebuild(stage);
    return HK_OK;
}

static hk_result_t k210_display_keep_last_clear(
    void *context, const hk_lease_t *lease)
{
    k210_display_state_t *state = (k210_display_state_t *)context;
    display_stage_t *stage;
    display_command_t command;
    hk_result_t result = stage_for(
        state, lease, DISPLAY_STAGE_BATCH, NULL, &stage);

    if(result != HK_OK)
        return result;
    if(stage->command_count == 0U ||
       stage->commands[stage->command_count - 1U].type != DISPLAY_COMMAND_CLEAR)
        return HK_ERR_INVALID_STATE;
    command = stage->commands[stage->command_count - 1U];
    if(stage->command_count > 1U)
    {
        stage->rollback_first = stage->commands[0];
        stage->rollback_first_valid = 1U;
    }
    stage->commands[0] = command;
    stage->command_count = 1U;
    stage->text_bytes = 0U;
    stage->borrow = (display_borrow_t){0};
    dirty_rebuild(stage);
    return HK_OK;
}

static hk_result_t k210_display_close(
    void *context, const hk_lease_t *lease, hk_deadline_t deadline)
{
    k210_display_state_t *state = (k210_display_state_t *)context;
    display_plane_t *plane = find_plane(state, lease);
    hk_display_rect_t cleanup[K210_DISPLAY_MAX_DIRTY_RECTS];
    uint16_t cleanup_count = 0U;
    uint8_t progress = 0U;
    hk_result_t result;

    if(!plane)
        return HK_ERR_INTERNAL;
    result = terminal_result(deadline, NULL);
    if(result != HK_OK)
        return result;
    if(state->stage.kind != DISPLAY_STAGE_NONE &&
       lease_equal(&state->stage.lease, lease))
    {
        stage_reset(&state->stage);
        workspace_release_if_idle(state);
    }
    if(state->surface.active &&
       lease_equal(&state->surface.lease, lease))
    {
        state->surface.lease = HK_LEASE_NONE;
        state->surface.dirty_count = 0U;
        state->surface.active = 0U;
    }
    for(uint16_t index = 0U; index < plane->repair_count; index++)
        (void)dirty_add(cleanup, &cleanup_count, &plane->repair[index]);
    if(plane->plane == HK_DISPLAY_PLANE_OVERLAY)
    {
        for(uint16_t index = 0U; index < state->overlay.dirty_count; index++)
            (void)dirty_add(cleanup, &cleanup_count,
                            &state->overlay.dirty[index]);
    }
    result = transfer_regions(
        cleanup, cleanup_count, NULL, NULL, deadline, NULL, &progress);
    if(result != HK_OK)
        return progress ? HK_ERR_INTERNAL : result;
    if(plane->plane == HK_DISPLAY_PLANE_OVERLAY)
        overlay_reset(&state->overlay);
    plane->lease = HK_LEASE_NONE;
    plane->repair_count = 0U;
    plane->active = 0U;
    workspace_release_if_idle(state);
    return HK_OK;
}

static hk_display_provider_t s_display_provider = {
    .context = &s_display,
    .open_plane = k210_display_open,
    .close_plane = k210_display_close,
    .get_info = k210_display_info,
    .begin_batch = k210_display_begin,
    .set_clip = k210_display_clip,
    .clear = k210_display_clear,
    .fill_rect = k210_display_fill,
    .stroke_rect = k210_display_stroke,
    .text = k210_display_text,
    .blit = k210_display_blit,
    .mark_dirty = k210_display_mark,
    .surface_acquire = k210_display_surface,
    .present = k210_display_present,
    .abort = k210_display_abort,
    .stage_checkpoint = k210_display_checkpoint,
    .stage_restore = k210_display_restore,
    .stage_keep_last_clear = k210_display_keep_last_clear,
};

static hk_result_t k210_display_cleanup_lease(
    void *context, const hk_lease_t *lease, hk_deadline_t deadline)
{
    hk_display_provider_t *provider = (hk_display_provider_t *)context;

    if(!provider || !provider->context)
        return HK_ERR_INTERNAL;
    if(!find_plane((k210_display_state_t *)provider->context, lease))
        return HK_OK;
    return k210_display_close(provider->context, lease, deadline);
}

static hk_result_t k210_display_cleanup(
    void *context, hk_owner_t owner, hk_deadline_t deadline)
{
    hk_display_provider_t *provider = (hk_display_provider_t *)context;
    k210_display_state_t *state;

    if(!provider || !provider->context)
        return HK_ERR_INTERNAL;
    state = (k210_display_state_t *)provider->context;
    for(uint16_t index = 0U; index < 2U; index++)
    {
        if(state->planes[index].active &&
           owner_equal(state->planes[index].lease.owner, owner))
        {
            hk_result_t result = k210_display_close(
                state, &state->planes[index].lease, deadline);
            if(result != HK_OK)
                return result;
        }
    }
    return HK_OK;
}

static hk_result_t k210_display_cleanup_dispatch(
    void *context, hk_owner_t owner, uint16_t target_core,
    hk_deadline_t deadline)
{
    if(target_core != 0U)
        return HK_ERR_WRONG_CONTEXT;
    return k210_display_cleanup(context, owner, deadline);
}

const hk_capability_provider_t hk_k210_display_provider = {
    .context = &s_display_provider,
    .cleanup_lease = k210_display_cleanup_lease,
    .cleanup = k210_display_cleanup,
    .cleanup_dispatch = k210_display_cleanup_dispatch,
    .max_leases = 2U,
};
