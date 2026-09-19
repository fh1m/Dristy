#ifndef HK_DISPLAY_NORMATIVE_SUITE_H
#define HK_DISPLAY_NORMATIVE_SUITE_H

#include <hackylens/capability/display.h>

typedef struct
{
    const char *implementation;
    void (*reset)(void);
    void (*fail_next_present)(hk_result_t result,
                              uint32_t after_transferred_slices);
    uint64_t (*transferred_bytes)(void);
    uint16_t (*panel_pixel)(uint32_t x, uint32_t y);
} hk_display_normative_fixture_t;

int hk_display_run_normative_suite(
    const hk_display_normative_fixture_t *fixture);

#endif
