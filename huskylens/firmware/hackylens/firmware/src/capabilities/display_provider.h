#ifndef HK_DISPLAY_PROVIDER_H
#define HK_DISPLAY_PROVIDER_H

#include <hackylens/capability/display.h>

typedef struct hk_display_provider hk_display_provider_t;

struct hk_display_provider
{
    void *context;
    hk_result_t (*open_plane)(void *context, const hk_lease_t *lease,
                              uint32_t plane);
    hk_result_t (*close_plane)(void *context, const hk_lease_t *lease,
                               hk_deadline_t deadline);
    hk_result_t (*get_info)(void *context, hk_display_info_t *info);
    hk_result_t (*begin_batch)(void *context, const hk_lease_t *lease);
    hk_result_t (*set_clip)(void *context, const hk_lease_t *lease,
                            const hk_display_rect_t *clip);
    hk_result_t (*clear)(void *context, const hk_lease_t *lease,
                         uint16_t color);
    hk_result_t (*fill_rect)(void *context, const hk_lease_t *lease,
                             const hk_display_rect_t *rect, uint16_t color);
    hk_result_t (*stroke_rect)(void *context, const hk_lease_t *lease,
                               const hk_display_rect_t *rect, uint16_t color);
    hk_result_t (*text)(void *context, const hk_lease_t *lease,
                        const hk_display_rect_t *bounds, const char *utf8,
                        uint32_t size_bytes, uint16_t color);
    hk_result_t (*blit)(void *context, const hk_lease_t *lease,
                        const hk_display_rect_t *destination,
                        const hk_buffer_view_t *pixels, uint32_t pixel_format);
    hk_result_t (*mark_dirty)(void *context, const hk_lease_t *lease,
                              const hk_display_rect_t *rect);
    hk_result_t (*surface_acquire)(void *context, const hk_lease_t *lease,
                                   hk_display_surface_t *surface);
    hk_result_t (*present)(void *context, const hk_lease_t *lease,
                           hk_deadline_t deadline, const hk_cancel_t *cancel);
    hk_result_t (*abort)(void *context, const hk_lease_t *lease);
    hk_result_t (*stage_checkpoint)(void *context, const hk_lease_t *lease,
                                    uint16_t *commands, uint16_t *text_bytes);
    hk_result_t (*stage_restore)(void *context, const hk_lease_t *lease,
                                 uint16_t commands, uint16_t text_bytes);
    hk_result_t (*stage_keep_last_clear)(void *context,
                                         const hk_lease_t *lease);
    uint32_t reserved;
};

#endif
