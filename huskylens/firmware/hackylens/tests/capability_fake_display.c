#include "capability_fake_display.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define FAKE_PLANE_COUNT 2U
#define FAKE_COMMAND_LOG_CAPACITY 64U
#define FAKE_SURFACE_BYTES \
    (HK_FAKE_DISPLAY_WIDTH * HK_FAKE_DISPLAY_HEIGHT * 2U)

enum
{
    FAKE_STAGE_NONE = 0,
    FAKE_STAGE_BATCH = 1,
    FAKE_STAGE_SURFACE = 2
};

typedef struct
{
    uint8_t active;
    uint8_t retired;
    uint8_t stage;
    uint8_t has_committed;
    uint8_t reserved[4];
    uint32_t plane;
    uint32_t generation;
    uint32_t committed_generation;
    hk_owner_t owner;
    hk_display_rect_t clip;
    hk_display_rect_t repair[HK_FAKE_DISPLAY_MAX_DIRTY_RECTS];
    hk_fake_display_command_t commands[HK_FAKE_DISPLAY_MAX_COMMANDS];
    hk_display_rect_t dirty[HK_FAKE_DISPLAY_MAX_DIRTY_RECTS];
    char text[HK_FAKE_DISPLAY_MAX_TEXT_BYTES];
    uint16_t command_count;
    uint16_t dirty_count;
    uint16_t text_bytes;
    uint16_t borrowed_views;
    uint16_t repair_count;
} fake_plane_t;

typedef struct
{
    uint8_t initialized;
    uint8_t quarantined;
    uint16_t reserved;
    hk_display_info_t info;
    fake_plane_t planes[FAKE_PLANE_COUNT];
    uint16_t surfaces[FAKE_PLANE_COUNT][FAKE_SURFACE_BYTES / 2U];
    uint16_t panels[FAKE_PLANE_COUNT][FAKE_SURFACE_BYTES / 2U];
    hk_fake_display_command_t command_log[FAKE_COMMAND_LOG_CAPACITY];
    uint32_t command_log_count;
    uint64_t now_us;
    uint32_t slice_duration_us;
    hk_result_t fail_result;
    uint32_t fail_after_slices;
    hk_fake_display_metrics_t metrics;
} fake_display_t;

static fake_display_t s_display;

static uint32_t plane_index(uint32_t plane);

static uint8_t owner_equal(hk_owner_t left, hk_owner_t right)
{
    return (uint8_t)(left.slot == right.slot &&
                     left.generation == right.generation);
}

static int version_compare(hk_version_t left, hk_version_t right)
{
    if(left.major != right.major)
        return left.major < right.major ? -1 : 1;
    if(left.minor != right.minor)
        return left.minor < right.minor ? -1 : 1;
    if(left.patch != right.patch)
        return left.patch < right.patch ? -1 : 1;
    return 0;
}

static hk_display_rect_t screen_rect(void)
{
    hk_display_rect_t result = {
        0, 0, s_display.info.width, s_display.info.height,
    };
    return result;
}

static void refresh_metrics(void)
{
    uint32_t active = 0U;
    uint32_t staged_commands = 0U;
    uint32_t staged_text = 0U;
    uint32_t staged_dirty = 0U;
    uint32_t borrowed = 0U;
    uint32_t repair = 0U;

    for(uint32_t index = 0U; index < FAKE_PLANE_COUNT; index++)
    {
        fake_plane_t *plane = &s_display.planes[index];
        if(plane->active)
            active |= plane->plane;
        staged_commands += plane->command_count;
        staged_text += plane->text_bytes;
        staged_dirty += plane->dirty_count;
        borrowed += plane->borrowed_views;
        if(plane->repair_count > 0U)
            repair |= plane->plane;
    }
    s_display.metrics.command_log_count = s_display.command_log_count;
    s_display.metrics.active_planes = active;
    s_display.metrics.staged_commands = staged_commands;
    s_display.metrics.staged_text_bytes = staged_text;
    s_display.metrics.staged_dirty_rects = staged_dirty;
    s_display.metrics.borrowed_views = borrowed;
    s_display.metrics.committed_base_generation =
        s_display.planes[0].committed_generation;
    s_display.metrics.committed_overlay_generation =
        s_display.planes[1].committed_generation;
    s_display.metrics.needs_repair_planes = repair;
    s_display.metrics.quarantined = s_display.quarantined;
}

void hk_fake_display_reset(uint32_t supported_planes)
{
    memset(&s_display, 0, sizeof(s_display));
    s_display.info = (hk_display_info_t){
        sizeof(hk_display_info_t), HK_DISPLAY_INFO_VERSION,
        HK_FAKE_DISPLAY_WIDTH, HK_FAKE_DISPLAY_HEIGHT,
        HK_DISPLAY_FORMAT_RGB565_BE,
        supported_planes & HK_DISPLAY_PLANE_ALL,
        2U, 2U,
        HK_FAKE_DISPLAY_MAX_COMMANDS,
        HK_FAKE_DISPLAY_MAX_TEXT_BYTES,
        HK_FAKE_DISPLAY_MAX_DIRTY_RECTS,
        HK_FAKE_DISPLAY_MAX_BORROWED_VIEWS,
        HK_FAKE_DISPLAY_TRANSFER_SLICE_BYTES,
        HK_FAKE_DISPLAY_MAX_PRESENT_US,
        0U,
    };
    s_display.planes[0].plane = HK_DISPLAY_PLANE_BASE;
    s_display.planes[1].plane = HK_DISPLAY_PLANE_OVERLAY;
    s_display.planes[0].generation = 1U;
    s_display.planes[1].generation = 1U;
    s_display.slice_duration_us = 1000U;
    s_display.initialized = 1U;
    refresh_metrics();
}

static void ensure_initialized(void)
{
    if(!s_display.initialized)
        hk_fake_display_reset(HK_DISPLAY_PLANE_ALL);
}

void hk_fake_display_set_now_us(uint64_t now_us)
{
    ensure_initialized();
    s_display.now_us = now_us;
}

void hk_fake_display_set_slice_duration_us(uint32_t duration_us)
{
    ensure_initialized();
    s_display.slice_duration_us = duration_us;
}

void hk_fake_display_fail_next_present(
    hk_result_t result, uint32_t after_transferred_slices)
{
    ensure_initialized();
    s_display.fail_result = result == HK_ERR_IO ? result : HK_OK;
    s_display.fail_after_slices = after_transferred_slices;
}

const hk_fake_display_metrics_t *hk_fake_display_metrics(void)
{
    ensure_initialized();
    refresh_metrics();
    return &s_display.metrics;
}

const hk_fake_display_command_t *hk_fake_display_command(uint32_t index)
{
    ensure_initialized();
    if(index >= s_display.command_log_count ||
       index >= FAKE_COMMAND_LOG_CAPACITY)
        return NULL;
    return &s_display.command_log[index];
}

uint16_t hk_fake_display_panel_pixel(
    uint32_t plane, uint32_t x, uint32_t y)
{
    const uint8_t *pixels;
    uint32_t offset;

    ensure_initialized();
    if((plane != HK_DISPLAY_PLANE_BASE &&
        plane != HK_DISPLAY_PLANE_OVERLAY) ||
       x >= s_display.info.width || y >= s_display.info.height)
        return 0U;
    pixels = (const uint8_t *)s_display.panels[plane_index(plane)];
    offset = (y * s_display.info.width + x) * 2U;
    return (uint16_t)((uint16_t)pixels[offset] << 8) | pixels[offset + 1U];
}

static hk_result_t request_validate(
    const hk_capability_request_t *request, uint32_t plane)
{
    hk_version_t provider_version = {0U, 1U, 0U, 0U};
    uint64_t required;

    if(!request || request->struct_size < sizeof(*request) ||
       request->struct_version != HK_CAPABILITY_REQUEST_VERSION ||
       request->id != HK_CAPABILITY_ID_DISPLAY || request->reserved != 0U ||
       request->minimum.reserved != 0U ||
       request->maximum_exclusive.reserved != 0U ||
       version_compare(request->minimum, request->maximum_exclusive) >= 0)
        return HK_ERR_INVALID_ARGUMENT;
    if(plane != HK_DISPLAY_PLANE_BASE &&
       plane != HK_DISPLAY_PLANE_OVERLAY)
        return HK_ERR_INVALID_ARGUMENT;
    if(version_compare(provider_version, request->minimum) < 0 ||
       version_compare(provider_version, request->maximum_exclusive) >= 0)
        return HK_ERR_VERSION_INCOMPATIBLE;
    required = request->required_features |
        (plane == HK_DISPLAY_PLANE_BASE ?
             HK_DISPLAY_FEATURE_BASE_PLANE :
             HK_DISPLAY_FEATURE_OVERLAY_PLANE);
    if((required & ~HK_DISPLAY_FEATURES_0_1) != 0U ||
       (required & ~(
           HK_DISPLAY_FEATURES_0_1 &
           (s_display.info.planes & HK_DISPLAY_PLANE_OVERLAY ?
                UINT64_MAX : ~HK_DISPLAY_FEATURE_OVERLAY_PLANE))) != 0U)
        return HK_ERR_FEATURE_UNAVAILABLE;
    if((s_display.info.planes & plane) == 0U)
        return HK_ERR_FEATURE_UNAVAILABLE;
    return HK_OK;
}

static uint32_t plane_index(uint32_t plane)
{
    return plane == HK_DISPLAY_PLANE_BASE ? 0U : 1U;
}

static hk_result_t validate_handle(
    hk_owner_t owner,
    const hk_display_t *handle,
    uint8_t allow_quarantined,
    fake_plane_t **result)
{
    fake_plane_t *plane;

    ensure_initialized();
    if(!handle)
        return HK_ERR_INVALID_ARGUMENT;
    if(handle->lease.capability_id != HK_CAPABILITY_ID_DISPLAY &&
       handle->lease.capability_id != 0U)
        return HK_ERR_INVALID_ARGUMENT;
    if(hk_lease_is_zero(&handle->lease) ||
       handle->lease.slot >= FAKE_PLANE_COUNT ||
       handle->lease.generation == 0U)
        return HK_ERR_STALE_HANDLE;
    if(!owner_equal(handle->lease.owner, owner))
        return HK_ERR_WRONG_OWNER;
    plane = &s_display.planes[handle->lease.slot];
    if(!plane->active || plane->generation != handle->lease.generation ||
       !owner_equal(plane->owner, owner))
        return HK_ERR_STALE_HANDLE;
    if(s_display.quarantined && !allow_quarantined)
        return HK_ERR_INVALID_STATE;
    if(result)
        *result = plane;
    return HK_OK;
}

hk_result_t hk_display_acquire(
    hk_owner_t owner,
    const hk_capability_request_t *request,
    uint32_t plane,
    hk_display_t *handle)
{
    fake_plane_t *slot;
    hk_result_t result;
    uint32_t index;

    ensure_initialized();
    if(!handle)
        return HK_ERR_INVALID_ARGUMENT;
    handle->lease = HK_LEASE_NONE;
    if(hk_owner_is_zero(owner))
        return HK_ERR_STALE_HANDLE;
    result = request_validate(request, plane);
    if(result != HK_OK)
        return result;
    if(s_display.quarantined)
        return HK_ERR_INVALID_STATE;
    index = plane_index(plane);
    slot = &s_display.planes[index];
    if(slot->retired)
        return HK_ERR_LIMIT;
    if(slot->active)
        return HK_ERR_BUSY;
    slot->active = 1U;
    slot->owner = owner;
    slot->clip = screen_rect();
    handle->lease = (hk_lease_t){
        index, slot->generation, owner, HK_CAPABILITY_ID_DISPLAY,
    };
    refresh_metrics();
    return HK_OK;
}

static hk_result_t rect_validate(const hk_display_rect_t *rect)
{
    int64_t right;
    int64_t bottom;

    if(!rect)
        return HK_ERR_INVALID_ARGUMENT;
    right = (int64_t)rect->x + rect->width;
    bottom = (int64_t)rect->y + rect->height;
    if(right > INT32_MAX || bottom > INT32_MAX)
        return HK_ERR_INVALID_ARGUMENT;
    return HK_OK;
}

static hk_display_rect_t rect_intersection(
    const hk_display_rect_t *left, const hk_display_rect_t *right)
{
    int64_t left_x = left->x > right->x ? left->x : right->x;
    int64_t left_y = left->y > right->y ? left->y : right->y;
    int64_t left_right = (int64_t)left->x + left->width;
    int64_t right_right = (int64_t)right->x + right->width;
    int64_t left_bottom = (int64_t)left->y + left->height;
    int64_t right_bottom = (int64_t)right->y + right->height;
    int64_t clipped_right = left_right < right_right ?
                            left_right : right_right;
    int64_t clipped_bottom = left_bottom < right_bottom ?
                             left_bottom : right_bottom;
    hk_display_rect_t result = {0, 0, 0U, 0U};

    if(clipped_right <= left_x || clipped_bottom <= left_y)
        return result;
    result.x = (int32_t)left_x;
    result.y = (int32_t)left_y;
    result.width = (uint32_t)(clipped_right - left_x);
    result.height = (uint32_t)(clipped_bottom - left_y);
    return result;
}

static uint8_t rect_empty(const hk_display_rect_t *rect)
{
    return (uint8_t)(rect->width == 0U || rect->height == 0U);
}

static uint8_t rect_overlaps(
    const hk_display_rect_t *left, const hk_display_rect_t *right)
{
    int64_t left_right = (int64_t)left->x + left->width;
    int64_t right_right = (int64_t)right->x + right->width;
    int64_t left_bottom = (int64_t)left->y + left->height;
    int64_t right_bottom = (int64_t)right->y + right->height;

    return (uint8_t)(left->x < right_right && right->x < left_right &&
                     left->y < right_bottom && right->y < left_bottom);
}

static hk_display_rect_t rect_union(
    const hk_display_rect_t *left, const hk_display_rect_t *right)
{
    int32_t x = left->x < right->x ? left->x : right->x;
    int32_t y = left->y < right->y ? left->y : right->y;
    int64_t left_right = (int64_t)left->x + left->width;
    int64_t right_right = (int64_t)right->x + right->width;
    int64_t left_bottom = (int64_t)left->y + left->height;
    int64_t right_bottom = (int64_t)right->y + right->height;
    int64_t end_x = left_right > right_right ? left_right : right_right;
    int64_t end_y = left_bottom > right_bottom ? left_bottom : right_bottom;
    hk_display_rect_t result = {
        x, y, (uint32_t)(end_x - x), (uint32_t)(end_y - y),
    };
    return result;
}

static hk_result_t dirty_preview(
    const fake_plane_t *plane,
    const hk_display_rect_t *rect,
    hk_display_rect_t *result,
    uint16_t *result_count)
{
    hk_display_rect_t candidate = *rect;
    uint16_t count = plane->dirty_count;

    memcpy(result, plane->dirty, sizeof(plane->dirty));
    if(rect_empty(rect))
    {
        *result_count = count;
        return HK_OK;
    }
    for(uint16_t index = 0U; index < count;)
    {
        if(!rect_overlaps(&candidate, &result[index]))
        {
            index++;
            continue;
        }
        candidate = rect_union(&candidate, &result[index]);
        count--;
        result[index] = result[count];
        index = 0U;
    }
    if(count >= s_display.info.maximum_dirty_rects)
        return HK_ERR_LIMIT;
    result[count++] = candidate;
    *result_count = count;
    return HK_OK;
}

static void append_log(const hk_fake_display_command_t *command)
{
    if(s_display.command_log_count < FAKE_COMMAND_LOG_CAPACITY)
        s_display.command_log[s_display.command_log_count++] = *command;
}

static hk_result_t append_command(
    fake_plane_t *plane, const hk_fake_display_command_t *command)
{
    hk_display_rect_t dirty[HK_FAKE_DISPLAY_MAX_DIRTY_RECTS];
    uint16_t dirty_count;
    hk_result_t result;

    if(rect_empty(&command->rect))
        return HK_OK;
    if(plane->command_count >= s_display.info.maximum_commands)
        return HK_ERR_LIMIT;
    result = dirty_preview(plane, &command->rect, dirty, &dirty_count);
    if(result != HK_OK)
        return result;
    plane->commands[plane->command_count++] = *command;
    memcpy(plane->dirty, dirty, sizeof(dirty));
    plane->dirty_count = dirty_count;
    append_log(command);
    refresh_metrics();
    return HK_OK;
}

static void clear_staged(fake_plane_t *plane)
{
    plane->stage = FAKE_STAGE_NONE;
    plane->command_count = 0U;
    plane->dirty_count = 0U;
    plane->text_bytes = 0U;
    plane->borrowed_views = 0U;
    plane->clip = screen_rect();
}

hk_result_t hk_display_get_info(
    hk_owner_t owner,
    const hk_display_t *handle,
    hk_display_info_t *info)
{
    hk_result_t result;

    if(!info)
        return HK_ERR_INVALID_ARGUMENT;
    result = validate_handle(owner, handle, 0U, NULL);
    if(result != HK_OK)
        return result;
    *info = s_display.info;
    return HK_OK;
}

hk_result_t hk_display_begin_batch(
    hk_owner_t owner, const hk_display_t *handle)
{
    fake_plane_t *plane;
    hk_result_t result = validate_handle(owner, handle, 0U, &plane);

    if(result != HK_OK)
        return result;
    if((HK_DISPLAY_FEATURES_0_1 & HK_DISPLAY_FEATURE_BATCH) == 0U)
        return HK_ERR_FEATURE_UNAVAILABLE;
    if(plane->stage != FAKE_STAGE_NONE)
        return HK_ERR_INVALID_STATE;
    clear_staged(plane);
    plane->stage = FAKE_STAGE_BATCH;
    refresh_metrics();
    return HK_OK;
}

hk_result_t hk_display_set_clip(
    hk_owner_t owner,
    const hk_display_t *handle,
    const hk_display_rect_t *clip)
{
    fake_plane_t *plane;
    hk_display_rect_t screen;
    hk_result_t result = validate_handle(owner, handle, 0U, &plane);

    if(result != HK_OK)
        return result;
    if(plane->stage != FAKE_STAGE_BATCH)
        return HK_ERR_INVALID_STATE;
    screen = screen_rect();
    if(!clip)
    {
        plane->clip = screen;
        return HK_OK;
    }
    result = rect_validate(clip);
    if(result != HK_OK)
        return result;
    plane->clip = rect_intersection(clip, &screen);
    return HK_OK;
}

hk_result_t hk_display_clear(
    hk_owner_t owner, const hk_display_t *handle, uint16_t rgb565)
{
    fake_plane_t *plane;
    hk_fake_display_command_t command;
    hk_result_t result = validate_handle(owner, handle, 0U, &plane);

    if(result != HK_OK)
        return result;
    if(plane->stage != FAKE_STAGE_BATCH)
        return HK_ERR_INVALID_STATE;
    command = (hk_fake_display_command_t){
        HK_FAKE_DISPLAY_COMMAND_CLEAR, plane->clip, rgb565,
        0U, 0U, 0U, NULL, 0U, 0U, 0U,
    };
    return append_command(plane, &command);
}

static hk_result_t rect_command(
    hk_owner_t owner,
    const hk_display_t *handle,
    const hk_display_rect_t *rect,
    uint16_t color,
    uint32_t type)
{
    fake_plane_t *plane;
    hk_display_rect_t clipped;
    hk_fake_display_command_t command;
    hk_result_t result = validate_handle(owner, handle, 0U, &plane);

    if(result != HK_OK)
        return result;
    if(plane->stage != FAKE_STAGE_BATCH)
        return HK_ERR_INVALID_STATE;
    result = rect_validate(rect);
    if(result != HK_OK)
        return result;
    clipped = rect_intersection(rect, &plane->clip);
    command = (hk_fake_display_command_t){
        type, clipped, color, 0U, 0U, 0U, NULL, 0U, 0U, 0U,
    };
    return append_command(plane, &command);
}

hk_result_t hk_display_fill_rect(
    hk_owner_t owner,
    const hk_display_t *handle,
    const hk_display_rect_t *rect,
    uint16_t rgb565)
{
    return rect_command(
        owner, handle, rect, rgb565, HK_FAKE_DISPLAY_COMMAND_FILL_RECT);
}

hk_result_t hk_display_stroke_rect(
    hk_owner_t owner,
    const hk_display_t *handle,
    const hk_display_rect_t *rect,
    uint16_t rgb565)
{
    return rect_command(
        owner, handle, rect, rgb565, HK_FAKE_DISPLAY_COMMAND_STROKE_RECT);
}

hk_result_t hk_display_text(
    hk_owner_t owner,
    const hk_display_t *handle,
    const hk_display_rect_t *bounds,
    const char *utf8,
    uint32_t size_bytes,
    uint16_t rgb565)
{
    fake_plane_t *plane;
    hk_display_rect_t clipped;
    hk_fake_display_command_t command;
    hk_result_t result = validate_handle(owner, handle, 0U, &plane);

    if(result != HK_OK)
        return result;
    if(plane->stage != FAKE_STAGE_BATCH)
        return HK_ERR_INVALID_STATE;
    result = rect_validate(bounds);
    if(result != HK_OK || (size_bytes > 0U && !utf8))
        return HK_ERR_INVALID_ARGUMENT;
    if(size_bytes == 0U || rect_empty(bounds))
        return HK_OK;
    if(size_bytes > (uint32_t)s_display.info.maximum_text_bytes -
                        plane->text_bytes)
        return HK_ERR_LIMIT;
    clipped = rect_intersection(bounds, &plane->clip);
    if(rect_empty(&clipped))
        return HK_OK;
    command = (hk_fake_display_command_t){
        HK_FAKE_DISPLAY_COMMAND_TEXT, clipped, rgb565, 0U,
        size_bytes, 0U, NULL, 0U, 0U, 0U,
    };
    result = append_command(plane, &command);
    if(result != HK_OK)
        return result;
    memcpy(&plane->text[plane->text_bytes], utf8, size_bytes);
    plane->text_bytes = (uint16_t)(plane->text_bytes + size_bytes);
    refresh_metrics();
    return HK_OK;
}

static hk_result_t pixel_view_validate(
    const hk_display_rect_t *destination,
    const hk_buffer_view_t *pixels,
    uint32_t pixel_format)
{
    uint64_t row_bytes;
    uint64_t required;

    if(pixel_format != HK_DISPLAY_FORMAT_RGB565_BE || !pixels ||
       !pixels->data ||
       (pixels->flags & HK_BUFFER_ACCESS_READABLE) == 0U)
        return HK_ERR_INVALID_ARGUMENT;
    row_bytes = (uint64_t)destination->width * 2U;
    if(pixels->stride_bytes < row_bytes ||
       pixels->stride_bytes % s_display.info.row_alignment_bytes != 0U ||
       (uintptr_t)pixels->data % s_display.info.buffer_alignment_bytes != 0U)
        return HK_ERR_INVALID_ARGUMENT;
    required = destination->height > 0U ?
        (uint64_t)(destination->height - 1U) * pixels->stride_bytes +
            row_bytes : 0U;
    if(required > pixels->size_bytes)
        return HK_ERR_INVALID_ARGUMENT;
    return HK_OK;
}

hk_result_t hk_display_blit(
    hk_owner_t owner,
    const hk_display_t *handle,
    const hk_display_rect_t *destination,
    const hk_buffer_view_t *pixels,
    uint32_t pixel_format)
{
    fake_plane_t *plane;
    hk_display_rect_t clipped;
    hk_fake_display_command_t command;
    uint32_t source_x;
    uint32_t source_y;
    uint64_t source_offset;
    hk_result_t result = validate_handle(owner, handle, 0U, &plane);

    if(result != HK_OK)
        return result;
    if(plane->stage != FAKE_STAGE_BATCH)
        return HK_ERR_INVALID_STATE;
    result = rect_validate(destination);
    if(result != HK_OK)
        return result;
    if(rect_empty(destination))
        return HK_OK;
    result = pixel_view_validate(destination, pixels, pixel_format);
    if(result != HK_OK)
        return result;
    if(plane->borrowed_views >= s_display.info.maximum_borrowed_views)
        return HK_ERR_LIMIT;
    clipped = rect_intersection(destination, &plane->clip);
    if(rect_empty(&clipped))
        return HK_OK;
    source_x = (uint32_t)((int64_t)clipped.x - destination->x);
    source_y = (uint32_t)((int64_t)clipped.y - destination->y);
    source_offset = (uint64_t)source_y * pixels->stride_bytes +
                    (uint64_t)source_x * 2U;
    command = (hk_fake_display_command_t){
        HK_FAKE_DISPLAY_COMMAND_BLIT, clipped, 0U, 0U,
        pixels->size_bytes, pixel_format,
        (const uint8_t *)pixels->data + source_offset,
        source_x, source_y, pixels->stride_bytes,
    };
    result = append_command(plane, &command);
    if(result != HK_OK)
        return result;
    plane->borrowed_views++;
    refresh_metrics();
    return HK_OK;
}

hk_result_t hk_display_mark_dirty(
    hk_owner_t owner,
    const hk_display_t *handle,
    const hk_display_rect_t *rect)
{
    fake_plane_t *plane;
    hk_display_rect_t clipped;
    hk_display_rect_t dirty[HK_FAKE_DISPLAY_MAX_DIRTY_RECTS];
    uint16_t dirty_count;
    hk_result_t result = validate_handle(owner, handle, 0U, &plane);

    if(result != HK_OK)
        return result;
    if(plane->stage == FAKE_STAGE_NONE)
        return HK_ERR_INVALID_STATE;
    result = rect_validate(rect);
    if(result != HK_OK)
        return result;
    clipped = plane->stage == FAKE_STAGE_BATCH ?
        rect_intersection(rect, &plane->clip) :
        rect_intersection(rect, &(hk_display_rect_t){
            0, 0, s_display.info.width, s_display.info.height,
        });
    result = dirty_preview(plane, &clipped, dirty, &dirty_count);
    if(result != HK_OK)
        return result;
    memcpy(plane->dirty, dirty, sizeof(dirty));
    plane->dirty_count = dirty_count;
    refresh_metrics();
    return HK_OK;
}

hk_result_t hk_display_surface_acquire(
    hk_owner_t owner,
    const hk_display_t *handle,
    hk_display_surface_t *surface)
{
    fake_plane_t *plane;
    uint32_t index;
    hk_result_t result;

    if(!surface)
        return HK_ERR_INVALID_ARGUMENT;
    result = validate_handle(owner, handle, 0U, &plane);
    if(result != HK_OK)
        return result;
    if((HK_DISPLAY_FEATURES_0_1 &
        HK_DISPLAY_FEATURE_BORROWED_SURFACE) == 0U)
        return HK_ERR_FEATURE_UNAVAILABLE;
    if(plane->stage != FAKE_STAGE_NONE)
        return HK_ERR_INVALID_STATE;
    clear_staged(plane);
    plane->stage = FAKE_STAGE_SURFACE;
    plane->borrowed_views = 1U;
    index = plane_index(plane->plane);
    *surface = (hk_display_surface_t){
        sizeof(hk_display_surface_t), HK_DISPLAY_SURFACE_VERSION,
        {
            s_display.surfaces[index], FAKE_SURFACE_BYTES,
            s_display.info.width * 2U,
            HK_BUFFER_ACCESS_READABLE | HK_BUFFER_ACCESS_WRITABLE,
        },
        s_display.info.width, s_display.info.height,
        HK_DISPLAY_FORMAT_RGB565_BE, 0U,
    };
    refresh_metrics();
    return HK_OK;
}

static hk_result_t terminal_before_effect(
    hk_deadline_t deadline, const hk_cancel_t *cancel)
{
    if(cancel && cancel->probe && cancel->probe(cancel->context))
        return HK_ERR_CANCELLED;
    if(deadline.at_us != 0U && s_display.now_us >= deadline.at_us)
        return HK_ERR_DEADLINE_EXCEEDED;
    return HK_OK;
}

static uint64_t rect_bytes(const hk_display_rect_t *rect)
{
    return (uint64_t)rect->width * rect->height * 2U;
}

static uint64_t rect_slices(const hk_display_rect_t *rect)
{
    uint64_t bytes = rect_bytes(rect);
    return bytes == 0U ? 0U :
        (bytes + s_display.info.transfer_slice_bytes - 1U) /
            s_display.info.transfer_slice_bytes;
}

static void sync_backing_chunk(
    const fake_plane_t *plane, const hk_display_rect_t *rect,
    uint64_t rect_offset, uint32_t size_bytes)
{
    uint32_t index = plane_index(plane->plane);
    const uint8_t *backing = (const uint8_t *)s_display.surfaces[index];
    uint8_t *panel = (uint8_t *)s_display.panels[index];

    for(uint32_t byte = 0U; byte < size_bytes; byte++)
    {
        uint64_t position = rect_offset + byte;
        uint64_t pixel = position / 2U;
        uint32_t x = (uint32_t)rect->x +
            (uint32_t)(pixel % rect->width);
        uint32_t y = (uint32_t)rect->y +
            (uint32_t)(pixel / rect->width);
        uint32_t offset = (y * s_display.info.width + x) * 2U +
            (uint32_t)(position & 1U);
        panel[offset] = backing[offset];
    }
}

static hk_result_t transfer_rect(
    const fake_plane_t *plane,
    const hk_display_rect_t *rect,
    hk_deadline_t deadline,
    const hk_cancel_t *cancel,
    uint8_t repair,
    uint8_t cleanup,
    uint8_t synchronize_backing,
    uint8_t allow_injected_failure,
    uint32_t *invocation_slices)
{
    uint64_t remaining = rect_bytes(rect);
    uint64_t rect_offset = 0U;

    while(remaining > 0U)
    {
        uint32_t chunk = remaining > s_display.info.transfer_slice_bytes ?
            s_display.info.transfer_slice_bytes : (uint32_t)remaining;
        hk_result_t result = terminal_before_effect(deadline, cancel);

        if(result != HK_OK)
            return result;
        if(allow_injected_failure && s_display.fail_result != HK_OK &&
           *invocation_slices >= s_display.fail_after_slices)
        {
            result = s_display.fail_result;
            s_display.fail_result = HK_OK;
            return result;
        }
        if(deadline.at_us != 0U &&
           s_display.slice_duration_us > deadline.at_us - s_display.now_us)
            return HK_ERR_DEADLINE_EXCEEDED;
        s_display.metrics.transferred_bytes += chunk;
        s_display.metrics.transfer_slices++;
        if(repair)
            s_display.metrics.repair_bytes += chunk;
        if(cleanup)
            s_display.metrics.cleanup_bytes += chunk;
        if(synchronize_backing)
            sync_backing_chunk(plane, rect, rect_offset, chunk);
        s_display.now_us += s_display.slice_duration_us;
        remaining -= chunk;
        rect_offset += chunk;
        (*invocation_slices)++;
    }
    if(!rect_empty(rect))
        s_display.metrics.transferred_regions++;
    return HK_OK;
}

static void write_fake_pixel(
    uint8_t *pixels, uint32_t x, uint32_t y, uint16_t color)
{
    uint32_t offset = (y * s_display.info.width + x) * 2U;
    pixels[offset] = (uint8_t)(color >> 8);
    pixels[offset + 1U] = (uint8_t)color;
}

static void apply_batch_to_backing(fake_plane_t *plane)
{
    uint32_t index = plane_index(plane->plane);
    uint8_t *backing = (uint8_t *)s_display.surfaces[index];
    uint8_t *panel = (uint8_t *)s_display.panels[index];

    for(uint16_t command_index = 0U;
        command_index < plane->command_count; command_index++)
    {
        const hk_fake_display_command_t *command =
            &plane->commands[command_index];
        for(uint32_t row = 0U; row < command->rect.height; row++)
        {
            uint32_t y = (uint32_t)command->rect.y + row;
            if(command->type == HK_FAKE_DISPLAY_COMMAND_BLIT)
            {
                const uint8_t *source =
                    (const uint8_t *)command->borrowed_data +
                    row * command->source_stride_bytes;
                uint32_t offset =
                    (y * s_display.info.width + (uint32_t)command->rect.x) * 2U;
                uint32_t size_bytes = command->rect.width * 2U;
                memcpy(backing + offset, source, size_bytes);
                memcpy(panel + offset, source, size_bytes);
                continue;
            }
            for(uint32_t column = 0U;
                column < command->rect.width; column++)
            {
                uint32_t x = (uint32_t)command->rect.x + column;
                uint8_t paint = (uint8_t)(
                    command->type == HK_FAKE_DISPLAY_COMMAND_CLEAR ||
                    command->type == HK_FAKE_DISPLAY_COMMAND_FILL_RECT ||
                    (command->type == HK_FAKE_DISPLAY_COMMAND_STROKE_RECT &&
                     (row == 0U || column == 0U ||
                      row + 1U == command->rect.height ||
                      column + 1U == command->rect.width)));
                if(paint)
                {
                    write_fake_pixel(backing, x, y, command->color);
                    write_fake_pixel(panel, x, y, command->color);
                }
            }
        }
    }
}

static uint8_t present_exceeds_limit(const fake_plane_t *plane)
{
    uint64_t slices = 0U;

    for(uint16_t index = 0U; index < plane->repair_count; index++)
        slices += rect_slices(&plane->repair[index]);
    for(uint16_t index = 0U; index < plane->dirty_count; index++)
        slices += rect_slices(&plane->dirty[index]);
    return (uint8_t)(slices * s_display.slice_duration_us >
                     s_display.info.maximum_present_duration_us);
}

hk_result_t hk_display_present(
    hk_owner_t owner,
    const hk_display_t *handle,
    hk_deadline_t deadline,
    const hk_cancel_t *cancel)
{
    fake_plane_t *plane;
    uint64_t staged_start_bytes;
    uint32_t invocation_slices = 0U;
    hk_result_t result;

    if(deadline.at_us == UINT64_MAX)
        return HK_ERR_INVALID_ARGUMENT;
    result = validate_handle(owner, handle, 0U, &plane);
    if(result != HK_OK)
        return result;
    if(plane->stage == FAKE_STAGE_NONE)
        return HK_ERR_INVALID_STATE;
    s_display.metrics.last_deadline = deadline;
    if(present_exceeds_limit(plane))
    {
        s_display.metrics.last_result = HK_ERR_LIMIT;
        return HK_ERR_LIMIT;
    }
    result = terminal_before_effect(deadline, cancel);
    if(result != HK_OK)
    {
        s_display.metrics.last_result = result;
        return result;
    }
    for(uint16_t index = 0U; index < plane->repair_count; index++)
    {
        result = transfer_rect(
            plane, &plane->repair[index], deadline, cancel,
            1U, 0U, 1U, 1U,
            &invocation_slices);
        if(result != HK_OK)
        {
            s_display.metrics.last_result = result;
            refresh_metrics();
            return result;
        }
    }
    plane->repair_count = 0U;
    staged_start_bytes = s_display.metrics.transferred_bytes;
    for(uint16_t index = 0U; index < plane->dirty_count; index++)
    {
        result = transfer_rect(
            plane, &plane->dirty[index], deadline, cancel,
            0U, 0U, (uint8_t)(plane->stage == FAKE_STAGE_SURFACE), 1U,
            &invocation_slices);
        if(result != HK_OK)
        {
            if(s_display.metrics.transferred_bytes > staged_start_bytes)
            {
                memcpy(plane->repair, plane->dirty, sizeof(plane->repair));
                plane->repair_count = plane->dirty_count;
            }
            s_display.metrics.last_result = result;
            refresh_metrics();
            return result;
        }
    }
    if(plane->stage == FAKE_STAGE_BATCH)
        apply_batch_to_backing(plane);
    if(plane->committed_generation != UINT32_MAX)
        plane->committed_generation++;
    plane->has_committed = 1U;
    clear_staged(plane);
    s_display.metrics.last_result = HK_OK;
    refresh_metrics();
    return HK_OK;
}

hk_result_t hk_display_abort(
    hk_owner_t owner, const hk_display_t *handle)
{
    fake_plane_t *plane;
    hk_result_t result = validate_handle(owner, handle, 0U, &plane);

    if(result != HK_OK)
        return result;
    if(plane->stage == FAKE_STAGE_NONE)
        return HK_ERR_INVALID_STATE;
    clear_staged(plane);
    refresh_metrics();
    return HK_OK;
}

static void retire_plane(fake_plane_t *plane, hk_display_t *handle)
{
    plane->active = 0U;
    plane->owner = HK_OWNER_NONE;
    clear_staged(plane);
    plane->repair_count = 0U;
    plane->has_committed = 0U;
    if(plane->generation == UINT32_MAX)
        plane->retired = 1U;
    else
        plane->generation++;
    handle->lease = HK_LEASE_NONE;
}

hk_result_t hk_display_release(
    hk_owner_t owner, hk_deadline_t deadline, hk_display_t *handle)
{
    fake_plane_t *plane;
    hk_display_rect_t overlay_cleanup;
    const hk_display_rect_t *cleanup_rects;
    uint16_t cleanup_count;
    uint64_t before_bytes;
    uint32_t invocation_slices = 0U;
    hk_result_t result;

    ensure_initialized();
    if(!handle || deadline.at_us == UINT64_MAX)
        return HK_ERR_INVALID_ARGUMENT;
    if(hk_lease_is_zero(&handle->lease))
        return HK_OK;
    result = validate_handle(owner, handle, 1U, &plane);
    if(result != HK_OK)
        return result;
    cleanup_rects = plane->repair;
    cleanup_count = plane->repair_count;
    if(plane->plane == HK_DISPLAY_PLANE_OVERLAY &&
       (plane->has_committed || plane->repair_count > 0U))
    {
        overlay_cleanup = screen_rect();
        cleanup_rects = &overlay_cleanup;
        cleanup_count = 1U;
    }
    s_display.metrics.last_deadline = deadline;
    if(cleanup_count > 0U)
    {
        result = terminal_before_effect(deadline, NULL);
        if(result != HK_OK)
        {
            s_display.metrics.last_result = result;
            return result;
        }
        before_bytes = s_display.metrics.transferred_bytes;
        for(uint16_t index = 0U; index < cleanup_count; index++)
        {
            result = transfer_rect(
                plane, &cleanup_rects[index], deadline, NULL,
                1U, 1U, 1U, 0U,
                &invocation_slices);
            if(result == HK_OK)
                continue;
            if(s_display.metrics.transferred_bytes == before_bytes)
            {
                s_display.metrics.last_result = result;
                return result;
            }
            s_display.quarantined = 1U;
            retire_plane(plane, handle);
            s_display.metrics.last_result = HK_ERR_INTERNAL;
            refresh_metrics();
            return HK_ERR_INTERNAL;
        }
    }
    retire_plane(plane, handle);
    s_display.metrics.last_result = HK_OK;
    refresh_metrics();
    return HK_OK;
}
