#include "display_normative_suite.h"

#include <stdint.h>
#include <stdio.h>

#define NORMATIVE_CHECK(condition)                                           \
    do                                                                       \
    {                                                                        \
        if(!(condition))                                                     \
        {                                                                    \
            fprintf(stderr, "DISPLAY_NORMATIVE_FAIL implementation=%s "    \
                            "line=%d\n",                                   \
                    fixture->implementation, __LINE__);                      \
            return 1;                                                        \
        }                                                                    \
    } while(0)

static hk_capability_request_t display_request(void)
{
    hk_capability_request_t request = HK_DISPLAY_REQUEST_0_1_INIT;
    return request;
}

static void surface_write(
    const hk_display_surface_t *surface, uint32_t x, uint32_t y,
    uint16_t pixel)
{
    uint8_t *destination = (uint8_t *)surface->pixels.data +
        y * surface->pixels.stride_bytes + x * 2U;
    destination[0] = (uint8_t)(pixel >> 8);
    destination[1] = (uint8_t)pixel;
}

static uint16_t surface_read(
    const hk_display_surface_t *surface, uint32_t x, uint32_t y)
{
    const uint8_t *source = (const uint8_t *)surface->pixels.data +
        y * surface->pixels.stride_bytes + x * 2U;
    return (uint16_t)((uint16_t)source[0] << 8) | source[1];
}

static int surface_mutate_then_abort(
    const hk_display_normative_fixture_t *fixture)
{
    hk_capability_request_t request = display_request();
    hk_owner_t owner = {101U, 1U};
    hk_display_t base;
    hk_display_surface_t surface;
    hk_display_rect_t pixel = {1, 1, 1U, 1U};

    fixture->reset();
    NORMATIVE_CHECK(hk_display_acquire(
        owner, &request, HK_DISPLAY_PLANE_BASE, &base) == HK_OK);
    NORMATIVE_CHECK(hk_display_surface_acquire(
        owner, &base, &surface) == HK_OK);
    surface_write(&surface, 1U, 1U, 0x1234U);
    NORMATIVE_CHECK(hk_display_mark_dirty(owner, &base, &pixel) == HK_OK);
    NORMATIVE_CHECK(hk_display_abort(owner, &base) == HK_OK);
    NORMATIVE_CHECK(surface_read(&surface, 1U, 1U) == 0x1234U);
    NORMATIVE_CHECK(fixture->panel_pixel(1U, 1U) == 0U);
    NORMATIVE_CHECK(hk_display_surface_acquire(
        owner, &base, &surface) == HK_OK);
    NORMATIVE_CHECK(surface_read(&surface, 1U, 1U) == 0x1234U);
    NORMATIVE_CHECK(hk_display_abort(owner, &base) == HK_OK);
    NORMATIVE_CHECK(hk_display_release(
        owner, HK_DEADLINE_IMMEDIATE, &base) == HK_OK);
    return 0;
}

static int retained_batch_abort_is_transactional(
    const hk_display_normative_fixture_t *fixture)
{
    hk_capability_request_t request = display_request();
    hk_owner_t owner = {108U, 1U};
    hk_display_t base;
    hk_display_surface_t surface;
    hk_display_rect_t pixel = {0, 0, 1U, 1U};

    fixture->reset();
    NORMATIVE_CHECK(hk_display_acquire(
        owner, &request, HK_DISPLAY_PLANE_BASE, &base) == HK_OK);
    NORMATIVE_CHECK(hk_display_surface_acquire(
        owner, &base, &surface) == HK_OK);
    surface_write(&surface, 0U, 0U, 0x1111U);
    NORMATIVE_CHECK(hk_display_mark_dirty(owner, &base, &pixel) == HK_OK);
    NORMATIVE_CHECK(hk_display_present(
        owner, &base, (hk_deadline_t){1000000U}, NULL) == HK_OK);
    NORMATIVE_CHECK(hk_display_begin_batch(owner, &base) == HK_OK);
    NORMATIVE_CHECK(hk_display_fill_rect(
        owner, &base, &pixel, 0x2222U) == HK_OK);
    NORMATIVE_CHECK(hk_display_abort(owner, &base) == HK_OK);
    NORMATIVE_CHECK(hk_display_surface_acquire(
        owner, &base, &surface) == HK_OK);
    NORMATIVE_CHECK(surface_read(&surface, 0U, 0U) == 0x1111U);
    NORMATIVE_CHECK(fixture->panel_pixel(0U, 0U) == 0x1111U);
    NORMATIVE_CHECK(hk_display_abort(owner, &base) == HK_OK);
    NORMATIVE_CHECK(hk_display_release(
        owner, HK_DEADLINE_IMMEDIATE, &base) == HK_OK);
    return 0;
}

static uint32_t failure_width(const hk_display_info_t *info)
{
    uint32_t width = info->transfer_slice_bytes / 2U + 1U;
    return width > info->width ? info->width : width;
}

static void fill_surface_row(
    const hk_display_surface_t *surface, uint32_t width, uint16_t pixel)
{
    for(uint32_t x = 0U; x < width; x++)
        surface_write(surface, x, 0U, pixel);
}

static int surface_partial_failure_retry(
    const hk_display_normative_fixture_t *fixture)
{
    hk_capability_request_t request = display_request();
    hk_owner_t owner = {102U, 1U};
    hk_display_t base;
    hk_display_info_t info;
    hk_display_surface_t surface;
    hk_display_rect_t dirty;
    uint32_t width;

    fixture->reset();
    NORMATIVE_CHECK(hk_display_acquire(
        owner, &request, HK_DISPLAY_PLANE_BASE, &base) == HK_OK);
    NORMATIVE_CHECK(hk_display_get_info(owner, &base, &info) == HK_OK);
    width = failure_width(&info);
    dirty = (hk_display_rect_t){0, 0, width, 1U};
    NORMATIVE_CHECK(hk_display_surface_acquire(
        owner, &base, &surface) == HK_OK);
    fill_surface_row(&surface, width, 0x2345U);
    NORMATIVE_CHECK(hk_display_mark_dirty(owner, &base, &dirty) == HK_OK);
    fixture->fail_next_present(HK_ERR_IO, 1U);
    NORMATIVE_CHECK(hk_display_present(
        owner, &base, (hk_deadline_t){1000000U}, NULL) == HK_ERR_IO);
    NORMATIVE_CHECK(surface_read(&surface, 0U, 0U) == 0x2345U &&
                    surface_read(&surface, width - 1U, 0U) == 0x2345U);
    NORMATIVE_CHECK(hk_display_present(
        owner, &base, (hk_deadline_t){1000000U}, NULL) == HK_OK);
    NORMATIVE_CHECK(fixture->panel_pixel(0U, 0U) == 0x2345U &&
                    fixture->panel_pixel(width - 1U, 0U) == 0x2345U);
    NORMATIVE_CHECK(hk_display_release(
        owner, HK_DEADLINE_IMMEDIATE, &base) == HK_OK);
    return 0;
}

static int surface_partial_failure_abort_release(
    const hk_display_normative_fixture_t *fixture)
{
    hk_capability_request_t request = display_request();
    hk_owner_t owner = {103U, 1U};
    hk_display_t base;
    hk_display_info_t info;
    hk_display_surface_t surface;
    hk_display_rect_t dirty;
    uint32_t width;
    uint64_t before;

    fixture->reset();
    NORMATIVE_CHECK(hk_display_acquire(
        owner, &request, HK_DISPLAY_PLANE_BASE, &base) == HK_OK);
    NORMATIVE_CHECK(hk_display_get_info(owner, &base, &info) == HK_OK);
    width = failure_width(&info);
    dirty = (hk_display_rect_t){0, 0, width, 1U};
    NORMATIVE_CHECK(hk_display_surface_acquire(
        owner, &base, &surface) == HK_OK);
    fill_surface_row(&surface, width, 0x3456U);
    NORMATIVE_CHECK(hk_display_mark_dirty(owner, &base, &dirty) == HK_OK);
    fixture->fail_next_present(HK_ERR_IO, 1U);
    NORMATIVE_CHECK(hk_display_present(
        owner, &base, (hk_deadline_t){1000000U}, NULL) == HK_ERR_IO);
    NORMATIVE_CHECK(hk_display_abort(owner, &base) == HK_OK);
    before = fixture->transferred_bytes();
    NORMATIVE_CHECK(hk_display_release(
        owner, (hk_deadline_t){1000000U}, &base) == HK_OK);
    NORMATIVE_CHECK(fixture->transferred_bytes() - before == width * 2U);
    NORMATIVE_CHECK(fixture->panel_pixel(0U, 0U) == 0x3456U &&
                    fixture->panel_pixel(width - 1U, 0U) == 0x3456U);
    return 0;
}

static int base_surface_and_overlay_coexist(
    const hk_display_normative_fixture_t *fixture)
{
    hk_capability_request_t request = display_request();
    hk_owner_t base_owner = {104U, 1U};
    hk_owner_t overlay_owner = {105U, 1U};
    hk_display_t base;
    hk_display_t overlay;
    hk_display_surface_t surface;
    hk_display_rect_t pixel = {0, 0, 1U, 1U};

    fixture->reset();
    NORMATIVE_CHECK(hk_display_acquire(
        base_owner, &request, HK_DISPLAY_PLANE_BASE, &base) == HK_OK);
    NORMATIVE_CHECK(hk_display_acquire(
        overlay_owner, &request, HK_DISPLAY_PLANE_OVERLAY, &overlay) == HK_OK);
    NORMATIVE_CHECK(hk_display_surface_acquire(
        base_owner, &base, &surface) == HK_OK);
    NORMATIVE_CHECK(hk_display_begin_batch(overlay_owner, &overlay) == HK_OK);
    NORMATIVE_CHECK(hk_display_clear(overlay_owner, &overlay, 0U) == HK_OK);
    surface_write(&surface, 0U, 0U, 0x4567U);
    NORMATIVE_CHECK(hk_display_mark_dirty(base_owner, &base, &pixel) == HK_OK);
    NORMATIVE_CHECK(hk_display_present(
        base_owner, &base, (hk_deadline_t){1000000U}, NULL) == HK_OK);
    NORMATIVE_CHECK(fixture->panel_pixel(0U, 0U) == 0x4567U);
    NORMATIVE_CHECK(hk_display_abort(overlay_owner, &overlay) == HK_OK);
    NORMATIVE_CHECK(hk_display_release(
        overlay_owner, HK_DEADLINE_IMMEDIATE, &overlay) == HK_OK);
    NORMATIVE_CHECK(hk_display_release(
        base_owner, HK_DEADLINE_IMMEDIATE, &base) == HK_OK);
    return 0;
}

static int disjoint_surface_repair(
    const hk_display_normative_fixture_t *fixture)
{
    hk_capability_request_t request = display_request();
    hk_owner_t owner = {106U, 1U};
    hk_display_t base;
    hk_display_info_t info;
    hk_display_surface_t surface;
    hk_display_rect_t first = {0, 0, 1U, 1U};
    hk_display_rect_t last;
    uint64_t before;

    fixture->reset();
    NORMATIVE_CHECK(hk_display_acquire(
        owner, &request, HK_DISPLAY_PLANE_BASE, &base) == HK_OK);
    NORMATIVE_CHECK(hk_display_get_info(owner, &base, &info) == HK_OK);
    last = (hk_display_rect_t){
        (int32_t)(info.width - 1U), (int32_t)(info.height - 1U), 1U, 1U,
    };
    NORMATIVE_CHECK(hk_display_surface_acquire(
        owner, &base, &surface) == HK_OK);
    surface_write(&surface, 0U, 0U, 0x5678U);
    surface_write(&surface, info.width - 1U, info.height - 1U, 0x6789U);
    NORMATIVE_CHECK(hk_display_mark_dirty(owner, &base, &first) == HK_OK);
    NORMATIVE_CHECK(hk_display_mark_dirty(owner, &base, &last) == HK_OK);
    fixture->fail_next_present(HK_ERR_IO, 1U);
    NORMATIVE_CHECK(hk_display_present(
        owner, &base, (hk_deadline_t){1000000U}, NULL) == HK_ERR_IO);
    before = fixture->transferred_bytes();
    NORMATIVE_CHECK(hk_display_present(
        owner, &base, (hk_deadline_t){1000000U}, NULL) == HK_OK);
    NORMATIVE_CHECK(fixture->transferred_bytes() - before == 8U);
    NORMATIVE_CHECK(fixture->panel_pixel(0U, 0U) == 0x5678U &&
                    fixture->panel_pixel(info.width - 1U,
                                         info.height - 1U) == 0x6789U);
    NORMATIVE_CHECK(hk_display_release(
        owner, HK_DEADLINE_IMMEDIATE, &base) == HK_OK);
    return 0;
}

static void write_source_pixel(uint8_t *pixels, uint32_t index, uint16_t value)
{
    pixels[index * 2U] = (uint8_t)(value >> 8);
    pixels[index * 2U + 1U] = (uint8_t)value;
}

static int clipped_blit_source_offsets(
    const hk_display_normative_fixture_t *fixture)
{
    hk_capability_request_t request = display_request();
    hk_owner_t owner = {107U, 1U};
    hk_display_t base;
    hk_display_rect_t destination = {-2, -1, 6U, 4U};
    uint32_t storage[16] = {0U};
    uint8_t *pixels = (uint8_t *)storage;
    hk_buffer_view_t view = {
        storage, sizeof(storage), 16U, HK_BUFFER_ACCESS_READABLE,
    };

    fixture->reset();
    for(uint32_t index = 0U; index < 32U; index++)
        write_source_pixel(pixels, index, (uint16_t)(0x7000U + index));
    NORMATIVE_CHECK(hk_display_acquire(
        owner, &request, HK_DISPLAY_PLANE_BASE, &base) == HK_OK);
    NORMATIVE_CHECK(hk_display_begin_batch(owner, &base) == HK_OK);
    NORMATIVE_CHECK(hk_display_blit(
        owner, &base, &destination, &view,
        HK_DISPLAY_FORMAT_RGB565_BE) == HK_OK);
    NORMATIVE_CHECK(hk_display_present(
        owner, &base, (hk_deadline_t){1000000U}, NULL) == HK_OK);
    NORMATIVE_CHECK(fixture->panel_pixel(0U, 0U) == 0x700AU);
    NORMATIVE_CHECK(fixture->panel_pixel(3U, 2U) == 0x701DU);
    NORMATIVE_CHECK(hk_display_release(
        owner, HK_DEADLINE_IMMEDIATE, &base) == HK_OK);
    return 0;
}

int hk_display_run_normative_suite(
    const hk_display_normative_fixture_t *fixture)
{
    if(!fixture || !fixture->implementation || !fixture->reset ||
       !fixture->fail_next_present || !fixture->transferred_bytes ||
       !fixture->panel_pixel)
        return 1;
    if(surface_mutate_then_abort(fixture) != 0 ||
       retained_batch_abort_is_transactional(fixture) != 0 ||
       surface_partial_failure_retry(fixture) != 0 ||
       surface_partial_failure_abort_release(fixture) != 0 ||
       base_surface_and_overlay_coexist(fixture) != 0 ||
       disjoint_surface_repair(fixture) != 0 ||
       clipped_blit_source_offsets(fixture) != 0)
        return 1;
    return 0;
}
