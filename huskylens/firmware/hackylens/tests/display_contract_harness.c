#include <hackylens/capability/display.h>

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "capability_fake_display.h"
#include "display_normative_suite.h"

#define CHECK(condition)                                                     \
    do                                                                       \
    {                                                                        \
        if(!(condition))                                                     \
        {                                                                    \
            printf("DISPLAY_FAIL line=%d\n", __LINE__);                  \
            return 1;                                                        \
        }                                                                    \
    } while(0)

typedef struct
{
    uint32_t calls;
    uint32_t cancel_on;
} cancel_state_t;

static uint8_t cancel_probe(const void *context)
{
    cancel_state_t *state = (cancel_state_t *)context;
    state->calls++;
    return (uint8_t)(state->cancel_on != 0U &&
                     state->calls >= state->cancel_on);
}

static hk_capability_request_t request(void)
{
    hk_capability_request_t result = HK_DISPLAY_REQUEST_0_1_INIT;
    return result;
}

static void normative_reset(void)
{
    hk_fake_display_reset(HK_DISPLAY_PLANE_ALL);
}

static void normative_fail(hk_result_t result, uint32_t after_slices)
{
    hk_fake_display_fail_next_present(result, after_slices);
}

static uint64_t normative_transferred_bytes(void)
{
    return hk_fake_display_metrics()->transferred_bytes;
}

static uint16_t normative_panel_pixel(uint32_t x, uint32_t y)
{
    return hk_fake_display_panel_pixel(HK_DISPLAY_PLANE_BASE, x, y);
}

static int test_info_planes_and_handles(void)
{
    hk_capability_request_t wanted = request();
    hk_capability_request_t unavailable = request();
    hk_owner_t owner_a = {1U, 1U};
    hk_owner_t owner_b = {2U, 1U};
    hk_display_t base;
    hk_display_t copied;
    hk_display_t other_base;
    hk_display_t overlay;
    hk_display_info_t info;

    hk_fake_display_reset(HK_DISPLAY_PLANE_ALL);
    CHECK(sizeof(hk_display_t) == sizeof(hk_lease_t));
    CHECK(hk_display_acquire(
        owner_a, &wanted, HK_DISPLAY_PLANE_BASE, &base) == HK_OK);
    copied = base;
    CHECK(hk_display_acquire(
        owner_b, &wanted, HK_DISPLAY_PLANE_BASE, &other_base) == HK_ERR_BUSY);
    CHECK(hk_display_acquire(
        owner_b, &wanted, HK_DISPLAY_PLANE_OVERLAY, &overlay) == HK_OK);
    CHECK(hk_display_get_info(owner_b, &base, &info) == HK_ERR_WRONG_OWNER);
    CHECK(hk_display_get_info(owner_a, &base, &info) == HK_OK);
    CHECK(info.width == HK_FAKE_DISPLAY_WIDTH &&
          info.height == HK_FAKE_DISPLAY_HEIGHT &&
          info.pixel_formats == HK_DISPLAY_FORMAT_RGB565_BE &&
          info.planes == HK_DISPLAY_PLANE_ALL &&
          info.maximum_commands == HK_FAKE_DISPLAY_MAX_COMMANDS &&
          info.maximum_text_bytes == HK_FAKE_DISPLAY_MAX_TEXT_BYTES &&
          info.maximum_dirty_rects == HK_FAKE_DISPLAY_MAX_DIRTY_RECTS &&
          info.maximum_borrowed_views ==
              HK_FAKE_DISPLAY_MAX_BORROWED_VIEWS &&
          info.maximum_present_duration_us ==
              HK_FAKE_DISPLAY_MAX_PRESENT_US);
    CHECK(hk_display_release(
        owner_b, HK_DEADLINE_IMMEDIATE, &overlay) == HK_OK);
    CHECK(hk_display_release(
        owner_a, HK_DEADLINE_IMMEDIATE, &base) == HK_OK);
    CHECK(hk_display_get_info(owner_a, &copied, &info) == HK_ERR_STALE_HANDLE);
    CHECK(hk_display_release(
        owner_a, HK_DEADLINE_IMMEDIATE, &base) == HK_OK);

    hk_fake_display_reset(HK_DISPLAY_PLANE_BASE);
    CHECK(hk_display_acquire(
        owner_a, &wanted, HK_DISPLAY_PLANE_OVERLAY, &overlay) ==
          HK_ERR_FEATURE_UNAVAILABLE);
    unavailable.required_features = UINT64_C(1) << 40;
    CHECK(hk_display_acquire(
        owner_a, &unavailable, HK_DISPLAY_PLANE_BASE, &base) ==
          HK_ERR_FEATURE_UNAVAILABLE);
    return 0;
}

static int test_clipping_noops_and_log(void)
{
    hk_capability_request_t wanted = request();
    hk_owner_t owner = {3U, 1U};
    hk_display_t base;
    hk_display_rect_t clip = {2, 2, 5U, 4U};
    hk_display_rect_t partial = {0, 0, 4U, 4U};
    hk_display_rect_t outside = {100, 100, 1U, 1U};
    hk_display_rect_t empty = {INT32_MAX, INT32_MAX, 0U, 0U};
    hk_display_rect_t overflow = {INT32_MAX, 0, 1U, 1U};
    const hk_fake_display_command_t *logged;
    const hk_fake_display_metrics_t *metrics;

    hk_fake_display_reset(HK_DISPLAY_PLANE_ALL);
    CHECK(hk_display_acquire(
        owner, &wanted, HK_DISPLAY_PLANE_BASE, &base) == HK_OK);
    CHECK(hk_display_begin_batch(owner, &base) == HK_OK);
    CHECK(hk_display_set_clip(owner, &base, &clip) == HK_OK);
    CHECK(hk_display_fill_rect(owner, &base, &partial, 0xF800U) == HK_OK);
    CHECK(hk_display_fill_rect(owner, &base, &outside, 0U) == HK_OK);
    CHECK(hk_display_fill_rect(owner, &base, &empty, 0U) == HK_OK);
    CHECK(hk_display_fill_rect(owner, &base, &overflow, 0U) ==
          HK_ERR_INVALID_ARGUMENT);
    metrics = hk_fake_display_metrics();
    CHECK(metrics->command_log_count == 1U &&
          metrics->staged_commands == 1U &&
          metrics->staged_dirty_rects == 1U);
    logged = hk_fake_display_command(0U);
    CHECK(logged && logged->type == HK_FAKE_DISPLAY_COMMAND_FILL_RECT &&
          logged->rect.x == 2 && logged->rect.y == 2 &&
          logged->rect.width == 2U && logged->rect.height == 2U);
    CHECK(hk_display_present(
        owner, &base, (hk_deadline_t){500000U}, NULL) == HK_OK);
    metrics = hk_fake_display_metrics();
    CHECK(metrics->transferred_bytes == 8U &&
          metrics->transferred_regions == 1U &&
          metrics->committed_base_generation == 1U &&
          metrics->staged_commands == 0U &&
          metrics->command_log_count == 1U);
    CHECK(hk_display_present(
        owner, &base, (hk_deadline_t){500000U}, NULL) ==
          HK_ERR_INVALID_STATE);
    CHECK(hk_display_release(
        owner, HK_DEADLINE_IMMEDIATE, &base) == HK_OK);
    return 0;
}

static int test_transactional_limits(void)
{
    hk_capability_request_t wanted = request();
    hk_owner_t owner = {4U, 1U};
    hk_display_t base;
    hk_display_rect_t same = {0, 0, 1U, 1U};
    hk_display_rect_t text_bounds = {0, 0, 8U, 8U};
    char text[HK_FAKE_DISPLAY_MAX_TEXT_BYTES];
    const hk_fake_display_metrics_t *metrics;

    memset(text, 'x', sizeof(text));
    hk_fake_display_reset(HK_DISPLAY_PLANE_ALL);
    CHECK(hk_display_acquire(
        owner, &wanted, HK_DISPLAY_PLANE_BASE, &base) == HK_OK);
    CHECK(hk_display_begin_batch(owner, &base) == HK_OK);
    for(uint32_t index = 0U; index < HK_FAKE_DISPLAY_MAX_COMMANDS; index++)
        CHECK(hk_display_fill_rect(owner, &base, &same, 0U) == HK_OK);
    CHECK(hk_display_fill_rect(owner, &base, &same, 0U) == HK_ERR_LIMIT);
    metrics = hk_fake_display_metrics();
    CHECK(metrics->staged_commands == HK_FAKE_DISPLAY_MAX_COMMANDS &&
          metrics->staged_dirty_rects == 1U);
    CHECK(hk_display_abort(owner, &base) == HK_OK);

    CHECK(hk_display_begin_batch(owner, &base) == HK_OK);
    CHECK(hk_display_text(
        owner, &base, &text_bounds, text, sizeof(text), 0xFFFFU) == HK_OK);
    CHECK(hk_display_text(
        owner, &base, &text_bounds, "y", 1U, 0xFFFFU) == HK_ERR_LIMIT);
    metrics = hk_fake_display_metrics();
    CHECK(metrics->staged_commands == 1U &&
          metrics->staged_text_bytes == HK_FAKE_DISPLAY_MAX_TEXT_BYTES);
    CHECK(hk_display_abort(owner, &base) == HK_OK);

    CHECK(hk_display_begin_batch(owner, &base) == HK_OK);
    for(uint32_t index = 0U; index < HK_FAKE_DISPLAY_MAX_DIRTY_RECTS; index++)
    {
        hk_display_rect_t dirty = {(int32_t)(index * 2U), 0, 1U, 1U};
        CHECK(hk_display_mark_dirty(owner, &base, &dirty) == HK_OK);
    }
    same.x = 8;
    CHECK(hk_display_mark_dirty(owner, &base, &same) == HK_ERR_LIMIT);
    CHECK(hk_fake_display_metrics()->staged_dirty_rects ==
          HK_FAKE_DISPLAY_MAX_DIRTY_RECTS);
    CHECK(hk_display_abort(owner, &base) == HK_OK);

    CHECK(hk_display_begin_batch(owner, &base) == HK_OK);
    CHECK(hk_display_clear(owner, &base, 0U) == HK_OK);
    hk_fake_display_set_slice_duration_us(20000U);
    CHECK(hk_display_present(
        owner, &base, (hk_deadline_t){500000U}, NULL) == HK_ERR_LIMIT);
    CHECK(hk_fake_display_metrics()->transferred_bytes == 0U &&
          hk_fake_display_metrics()->staged_commands == 1U);
    hk_fake_display_set_slice_duration_us(1000U);
    CHECK(hk_display_abort(owner, &base) == HK_OK);
    CHECK(hk_display_release(
        owner, HK_DEADLINE_IMMEDIATE, &base) == HK_OK);
    return 0;
}

static int test_cancel_retry_and_deadline(void)
{
    hk_capability_request_t wanted = request();
    hk_owner_t owner = {5U, 1U};
    hk_display_t base;
    hk_display_rect_t dirty = {0, 0, 8U, 2U};
    cancel_state_t cancel_state = {0U, 3U};
    hk_cancel_t cancel = {cancel_probe, &cancel_state};
    const hk_fake_display_metrics_t *metrics;
    uint64_t bytes;

    hk_fake_display_reset(HK_DISPLAY_PLANE_ALL);
    CHECK(hk_display_acquire(
        owner, &wanted, HK_DISPLAY_PLANE_BASE, &base) == HK_OK);
    CHECK(hk_display_begin_batch(owner, &base) == HK_OK);
    CHECK(hk_display_fill_rect(owner, &base, &dirty, 0x07E0U) == HK_OK);
    CHECK(hk_display_present(
        owner, &base, (hk_deadline_t){500000U}, &cancel) ==
          HK_ERR_CANCELLED);
    metrics = hk_fake_display_metrics();
    CHECK(metrics->transferred_bytes == 8U &&
          metrics->needs_repair_planes == HK_DISPLAY_PLANE_BASE &&
          metrics->staged_commands == 1U &&
          metrics->committed_base_generation == 0U &&
          metrics->last_result == HK_ERR_CANCELLED);
    CHECK(hk_display_present(
        owner, &base, (hk_deadline_t){500000U}, NULL) == HK_OK);
    metrics = hk_fake_display_metrics();
    CHECK(metrics->transferred_bytes == 72U &&
          metrics->repair_bytes == 32U &&
          metrics->needs_repair_planes == 0U &&
          metrics->committed_base_generation == 1U);

    CHECK(hk_display_begin_batch(owner, &base) == HK_OK);
    CHECK(hk_display_fill_rect(owner, &base, &dirty, 0U) == HK_OK);
    hk_fake_display_set_now_us(600000U);
    bytes = hk_fake_display_metrics()->transferred_bytes;
    CHECK(hk_display_present(
        owner, &base, (hk_deadline_t){600000U}, NULL) ==
          HK_ERR_DEADLINE_EXCEEDED);
    CHECK(hk_fake_display_metrics()->transferred_bytes == bytes &&
          hk_fake_display_metrics()->needs_repair_planes == 0U &&
          hk_fake_display_metrics()->staged_commands == 1U);
    CHECK(hk_display_abort(owner, &base) == HK_OK);
    CHECK(hk_display_release(
        owner, HK_DEADLINE_IMMEDIATE, &base) == HK_OK);
    return 0;
}

static int test_surface_blit_and_incremental_bytes(void)
{
    hk_capability_request_t wanted = request();
    hk_owner_t owner = {6U, 1U};
    hk_display_t base;
    hk_display_surface_t surface;
    hk_display_rect_t dirty = {1, 1, 2U, 2U};
    hk_display_rect_t destination = {0, 0, 4U, 2U};
    uint16_t pixels[8] = {0U};
    uint8_t raw[32] = {0U};
    hk_buffer_view_t view = {
        pixels, sizeof(pixels), 8U, HK_BUFFER_ACCESS_READABLE,
    };
    hk_buffer_view_t invalid = view;
    uint64_t before;

    hk_fake_display_reset(HK_DISPLAY_PLANE_ALL);
    CHECK(hk_display_acquire(
        owner, &wanted, HK_DISPLAY_PLANE_BASE, &base) == HK_OK);
    CHECK(hk_display_surface_acquire(owner, &base, &surface) == HK_OK);
    CHECK(surface.width == HK_FAKE_DISPLAY_WIDTH &&
          surface.height == HK_FAKE_DISPLAY_HEIGHT &&
          surface.pixel_format == HK_DISPLAY_FORMAT_RGB565_BE &&
          surface.pixels.size_bytes ==
              HK_FAKE_DISPLAY_WIDTH * HK_FAKE_DISPLAY_HEIGHT * 2U &&
          surface.pixels.stride_bytes == HK_FAKE_DISPLAY_WIDTH * 2U &&
          (surface.pixels.flags & HK_BUFFER_ACCESS_WRITABLE) != 0U &&
          (uintptr_t)surface.pixels.data % 2U == 0U);
    CHECK(hk_display_mark_dirty(owner, &base, &dirty) == HK_OK);
    CHECK(hk_display_present(
        owner, &base, (hk_deadline_t){500000U}, NULL) == HK_OK);
    CHECK(hk_fake_display_metrics()->transferred_bytes == 8U &&
          hk_fake_display_metrics()->borrowed_views == 0U);

    CHECK(hk_display_begin_batch(owner, &base) == HK_OK);
    invalid.data = (void *)((uintptr_t)raw | (uintptr_t)1U);
    CHECK(hk_display_blit(
        owner, &base, &destination, &invalid,
        HK_DISPLAY_FORMAT_RGB565_BE) == HK_ERR_INVALID_ARGUMENT);
    invalid = view;
    invalid.size_bytes--;
    CHECK(hk_display_blit(
        owner, &base, &destination, &invalid,
        HK_DISPLAY_FORMAT_RGB565_BE) == HK_ERR_INVALID_ARGUMENT);
    CHECK(hk_display_blit(
        owner, &base, &destination, &view,
        HK_DISPLAY_FORMAT_RGB565_BE) == HK_OK);
    CHECK(hk_fake_display_metrics()->borrowed_views == 1U);
    before = hk_fake_display_metrics()->transferred_bytes;
    hk_fake_display_fail_next_present(HK_ERR_IO, 1U);
    CHECK(hk_display_present(
        owner, &base, (hk_deadline_t){500000U}, NULL) == HK_ERR_IO);
    CHECK(hk_fake_display_metrics()->transferred_bytes - before == 8U &&
          hk_fake_display_metrics()->borrowed_views == 1U &&
          hk_fake_display_metrics()->needs_repair_planes ==
              HK_DISPLAY_PLANE_BASE);
    CHECK(hk_display_present(
        owner, &base, (hk_deadline_t){500000U}, NULL) == HK_OK);
    CHECK(hk_fake_display_metrics()->transferred_bytes - before == 40U &&
          hk_fake_display_metrics()->borrowed_views == 0U);
    CHECK(40U < HK_FAKE_DISPLAY_WIDTH * HK_FAKE_DISPLAY_HEIGHT * 2U);
    CHECK(hk_display_release(
        owner, HK_DEADLINE_IMMEDIATE, &base) == HK_OK);
    return 0;
}

static int test_clipped_blit_source_mapping(void)
{
    hk_capability_request_t wanted = request();
    hk_owner_t owner = {8U, 1U};
    hk_display_t base;
    hk_display_rect_t destination = {-2, -1, 6U, 4U};
    uint16_t pixels[32] = {0U};
    hk_buffer_view_t view = {
        pixels, 60U, 16U, HK_BUFFER_ACCESS_READABLE,
    };
    const hk_fake_display_command_t *logged;

    hk_fake_display_reset(HK_DISPLAY_PLANE_ALL);
    CHECK(hk_display_acquire(
        owner, &wanted, HK_DISPLAY_PLANE_BASE, &base) == HK_OK);
    CHECK(hk_display_begin_batch(owner, &base) == HK_OK);
    CHECK(hk_display_blit(
        owner, &base, &destination, &view,
        HK_DISPLAY_FORMAT_RGB565_BE) == HK_OK);
    logged = hk_fake_display_command(0U);
    CHECK(logged && logged->type == HK_FAKE_DISPLAY_COMMAND_BLIT &&
          logged->rect.x == 0 && logged->rect.y == 0 &&
          logged->rect.width == 4U && logged->rect.height == 3U &&
          logged->source_x == 2U && logged->source_y == 1U &&
          logged->source_stride_bytes == 16U &&
          logged->borrowed_data == (const uint8_t *)pixels + 20U);
    CHECK(hk_display_present(
        owner, &base, (hk_deadline_t){500000U}, NULL) == HK_OK);
    CHECK(hk_fake_display_metrics()->transferred_bytes == 24U);
    CHECK(hk_display_release(
        owner, HK_DEADLINE_IMMEDIATE, &base) == HK_OK);
    return 0;
}

static int test_disjoint_damage_stays_bounded_during_repair(void)
{
    hk_capability_request_t wanted = request();
    hk_owner_t owner = {9U, 1U};
    hk_display_t base;
    hk_display_rect_t first = {0, 0, 1U, 1U};
    hk_display_rect_t last = {
        HK_FAKE_DISPLAY_WIDTH - 1, HK_FAKE_DISPLAY_HEIGHT - 1, 1U, 1U,
    };
    uint64_t before;

    hk_fake_display_reset(HK_DISPLAY_PLANE_ALL);
    CHECK(hk_display_acquire(
        owner, &wanted, HK_DISPLAY_PLANE_BASE, &base) == HK_OK);
    CHECK(hk_display_begin_batch(owner, &base) == HK_OK);
    CHECK(hk_display_fill_rect(owner, &base, &first, 0U) == HK_OK);
    CHECK(hk_display_fill_rect(owner, &base, &last, 0U) == HK_OK);
    CHECK(hk_fake_display_metrics()->staged_dirty_rects == 2U);
    hk_fake_display_fail_next_present(HK_ERR_IO, 1U);
    CHECK(hk_display_present(
        owner, &base, (hk_deadline_t){500000U}, NULL) == HK_ERR_IO);
    CHECK(hk_fake_display_metrics()->transferred_bytes == 2U &&
          hk_fake_display_metrics()->needs_repair_planes ==
              HK_DISPLAY_PLANE_BASE);
    before = hk_fake_display_metrics()->transferred_bytes;
    CHECK(hk_display_present(
        owner, &base, (hk_deadline_t){500000U}, NULL) == HK_OK);
    CHECK(hk_fake_display_metrics()->transferred_bytes - before == 8U &&
          hk_fake_display_metrics()->repair_bytes == 4U &&
          hk_fake_display_metrics()->needs_repair_planes == 0U &&
          hk_fake_display_metrics()->committed_base_generation == 1U);
    CHECK(hk_fake_display_metrics()->repair_bytes <
          HK_FAKE_DISPLAY_WIDTH * HK_FAKE_DISPLAY_HEIGHT * 2U);
    CHECK(hk_display_release(
        owner, HK_DEADLINE_IMMEDIATE, &base) == HK_OK);
    return 0;
}

static int test_partial_failure_abort_cleanup(void)
{
    hk_capability_request_t wanted = request();
    hk_owner_t owner = {7U, 1U};
    hk_display_t base;
    hk_display_t overlay;
    hk_display_rect_t dirty = {0, 0, 8U, 2U};
    uint64_t before;

    hk_fake_display_reset(HK_DISPLAY_PLANE_ALL);
    CHECK(hk_display_acquire(
        owner, &wanted, HK_DISPLAY_PLANE_BASE, &base) == HK_OK);
    CHECK(hk_display_begin_batch(owner, &base) == HK_OK);
    CHECK(hk_display_fill_rect(owner, &base, &dirty, 0x001FU) == HK_OK);
    hk_fake_display_fail_next_present(HK_ERR_IO, 1U);
    CHECK(hk_display_present(
        owner, &base, (hk_deadline_t){500000U}, NULL) == HK_ERR_IO);
    CHECK(hk_fake_display_metrics()->transferred_bytes == 8U &&
          hk_fake_display_metrics()->needs_repair_planes ==
              HK_DISPLAY_PLANE_BASE &&
          hk_fake_display_metrics()->staged_commands == 1U);
    CHECK(hk_display_abort(owner, &base) == HK_OK);
    CHECK(hk_fake_display_metrics()->staged_commands == 0U &&
          hk_fake_display_metrics()->needs_repair_planes ==
              HK_DISPLAY_PLANE_BASE);
    CHECK(hk_display_release(
        owner, HK_DEADLINE_IMMEDIATE, &base) == HK_OK);
    CHECK(hk_fake_display_metrics()->cleanup_bytes == 32U &&
          hk_fake_display_metrics()->needs_repair_planes == 0U);

    hk_fake_display_reset(HK_DISPLAY_PLANE_ALL);
    CHECK(hk_display_acquire(
        owner, &wanted, HK_DISPLAY_PLANE_OVERLAY, &overlay) == HK_OK);
    CHECK(hk_display_begin_batch(owner, &overlay) == HK_OK);
    CHECK(hk_display_clear(owner, &overlay, 0U) == HK_OK);
    CHECK(hk_display_present(
        owner, &overlay, (hk_deadline_t){500000U}, NULL) == HK_OK);
    before = hk_fake_display_metrics()->transferred_bytes;
    CHECK(before == HK_FAKE_DISPLAY_WIDTH * HK_FAKE_DISPLAY_HEIGHT * 2U);
    CHECK(hk_display_release(
        owner, (hk_deadline_t){48000U}, &overlay) ==
          HK_ERR_DEADLINE_EXCEEDED);
    CHECK(hk_fake_display_metrics()->transferred_bytes == before &&
          !hk_lease_is_zero(&overlay.lease));
    CHECK(hk_display_release(
        owner, HK_DEADLINE_IMMEDIATE, &overlay) == HK_OK);
    CHECK(hk_fake_display_metrics()->cleanup_bytes ==
          HK_FAKE_DISPLAY_WIDTH * HK_FAKE_DISPLAY_HEIGHT * 2U);
    return 0;
}

int main(void)
{
    const hk_display_normative_fixture_t normative = {
        "fake", normative_reset, normative_fail,
        normative_transferred_bytes, normative_panel_pixel,
    };

    CHECK(hk_display_run_normative_suite(&normative) == 0);
    CHECK(test_info_planes_and_handles() == 0);
    CHECK(test_clipping_noops_and_log() == 0);
    CHECK(test_transactional_limits() == 0);
    CHECK(test_cancel_retry_and_deadline() == 0);
    CHECK(test_surface_blit_and_incremental_bytes() == 0);
    CHECK(test_clipped_blit_source_mapping() == 0);
    CHECK(test_disjoint_damage_stays_bounded_during_repair() == 0);
    CHECK(test_partial_failure_abort_cleanup() == 0);
    puts("DISPLAY_CONTRACT_OK cases=15 normative=7 full_bytes=384 slice_bytes=8");
    return 0;
}
