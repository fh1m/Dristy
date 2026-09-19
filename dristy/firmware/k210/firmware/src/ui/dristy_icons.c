#include "dristy_icons.h"

#include <string.h>

#include "display_binding.h"
#include "dristy_theme.h"

static void fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t c)
{
    hk_ui_display_fill_rect(x, y, w, h, c);
}

static void frame(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                  uint16_t t, uint16_t c)
{
    hk_ui_display_draw_rect(x, y, w, h, t, c);
}

static void icon_terminal(uint16_t ox, uint16_t oy, uint16_t s,
                          uint16_t fg, uint16_t bg)
{
    uint16_t m = (uint16_t)(s / 8U);
    fill(ox + m, oy + m, s - 2U * m, s - 2U * m, bg);
    frame(ox + m, oy + m, s - 2U * m, s - 2U * m, 2U, fg);
    fill(ox + m + 4U, oy + m + 4U, s - 2U * m - 8U, (uint16_t)(s / 5U), fg);
    for(uint8_t i = 0; i < 4U; i++)
        fill(ox + m + 6U, oy + m + 14U + i * 8U, s - 2U * m - 12U, 3U, fg);
}

static void icon_camera(uint16_t ox, uint16_t oy, uint16_t s,
                        uint16_t fg, uint16_t bg)
{
    uint16_t b = (uint16_t)(s / 6U);
    fill(ox + b, oy + b + 6U, s - 2U * b, s - 2U * b - 8U, bg);
    frame(ox + b, oy + b + 6U, s - 2U * b, s - 2U * b - 8U, 2U, fg);
    fill(ox + (s / 2U) - 6U, oy + b, 12U, 8U, fg);
    fill(ox + (s / 2U) - 10U, oy + (s / 2U), 20U, 20U, bg);
    frame(ox + (s / 2U) - 10U, oy + (s / 2U), 20U, 20U, 2U, fg);
    fill(ox + (s / 2U) - 4U, oy + (s / 2U) + 6U, 8U, 8U, DRISTY_COLOR_TEXT);
}

static void icon_qr(uint16_t ox, uint16_t oy, uint16_t s,
                    uint16_t fg, uint16_t bg)
{
    uint16_t q = (uint16_t)(s / 3U);
    fill(ox + 8U, oy + 8U, q, q, fg);
    fill(ox + 10U, oy + 10U, q - 4U, q - 4U, bg);
    fill(ox + s - 8U - q, oy + 8U, q, q, fg);
    fill(ox + s - 6U - q, oy + 10U, q - 4U, q - 4U, bg);
    fill(ox + 8U, oy + s - 8U - q, q, q, fg);
    fill(ox + 10U, oy + s - 6U - q, q - 4U, q - 4U, bg);
    for(uint8_t i = 0; i < 5U; i++)
        fill(ox + s / 2U - 2U, oy + 14U + i * 6U, 4U, 4U, fg);
    for(uint8_t i = 0; i < 4U; i++)
        fill(ox + 20U + i * 7U, oy + s - 18U, 4U, 4U, fg);
}

static void icon_face(uint16_t ox, uint16_t oy, uint16_t s,
                      uint16_t fg, uint16_t bg)
{
    (void)bg;
    frame(ox + 10U, oy + 8U, s - 20U, s - 16U, 2U, fg);
    fill(ox + 18U, oy + 22U, 6U, 6U, fg);
    fill(ox + s - 24U, oy + 22U, 6U, 6U, fg);
    fill(ox + s / 2U - 12U, oy + s - 22U, 24U, 3U, fg);
    fill(ox + s / 2U - 4U, oy + s - 18U, 8U, 8U, DRISTY_COLOR_ACCENT);
}

static void icon_tag(uint16_t ox, uint16_t oy, uint16_t s,
                     uint16_t fg, uint16_t bg)
{
    fill(ox + 12U, oy + 14U, s - 24U, s - 28U, bg);
    frame(ox + 12U, oy + 14U, s - 24U, s - 28U, 2U, fg);
    fill(ox + s / 2U - 2U, oy + 10U, 4U, 8U, fg);
    fill(ox + 18U, oy + 22U, 8U, 8U, fg);
    fill(ox + 20U, oy + 24U, 4U, 4U, bg);
}

static void icon_object(uint16_t ox, uint16_t oy, uint16_t s,
                        uint16_t fg, uint16_t bg)
{
    frame(ox + 8U, oy + 12U, s - 16U, s - 20U, 2U, fg);
    fill(ox + 14U, oy + 18U, s - 28U, s - 32U, bg);
    fill(ox + 16U, oy + 20U, s - 32U, 4U, fg);
    fill(ox + 16U, oy + 28U, (uint16_t)(s / 2U), 4U, fg);
    fill(ox + 16U, oy + 36U, s - 36U, 4U, fg);
}

static void icon_files(uint16_t ox, uint16_t oy, uint16_t s,
                       uint16_t fg, uint16_t bg)
{
    fill(ox + 14U, oy + 16U, s - 28U, s - 24U, bg);
    frame(ox + 14U, oy + 16U, s - 28U, s - 24U, 2U, fg);
    fill(ox + 14U, oy + 16U, s - 28U, 8U, fg);
    for(uint8_t i = 0; i < 3U; i++)
        fill(ox + 18U, oy + 30U + i * 8U, s - 36U, 3U, fg);
}

static void icon_buttons(uint16_t ox, uint16_t oy, uint16_t s,
                         uint16_t fg, uint16_t bg)
{
    fill(ox + 10U, oy + 18U, 14U, 10U, bg);
    frame(ox + 10U, oy + 18U, 14U, 10U, 2U, fg);
    fill(ox + s - 24U, oy + 18U, 14U, 10U, bg);
    frame(ox + s - 24U, oy + 18U, 14U, 10U, 2U, fg);
    fill(ox + s / 2U - 8U, oy + s - 28U, 16U, 10U, bg);
    frame(ox + s / 2U - 8U, oy + s - 28U, 16U, 10U, 2U, fg);
}

static void icon_pong(uint16_t ox, uint16_t oy, uint16_t s,
                      uint16_t fg, uint16_t bg)
{
    (void)bg;
    frame(ox + 12U, oy + 14U, s - 24U, s - 28U, 2U, fg);
    fill(ox + 16U, oy + 18U, 4U, (uint16_t)(s - 36U), fg);
    fill(ox + s - 20U, oy + 18U, 4U, (uint16_t)(s - 36U), fg);
    fill(ox + s / 2U - 4U, oy + s / 2U - 4U, 8U, 8U, fg);
}

static void icon_settings(uint16_t ox, uint16_t oy, uint16_t s,
                          uint16_t fg, uint16_t bg)
{
    uint16_t cx = ox + s / 2U;
    uint16_t cy = oy + s / 2U;
    fill(cx - 12U, cy - 12U, 24U, 24U, bg);
    frame(cx - 12U, cy - 12U, 24U, 24U, 2U, fg);
    fill(cx - 3U, cy - 16U, 6U, 32U, fg);
    fill(cx - 16U, cy - 3U, 32U, 6U, fg);
    fill(cx - 4U, cy - 4U, 8U, 8U, bg);
}

static void icon_sleep(uint16_t ox, uint16_t oy, uint16_t s,
                       uint16_t fg, uint16_t bg)
{
    (void)bg;
    fill(ox + 14U, oy + 20U, s - 28U, 3U, fg);
    fill(ox + 18U, oy + 24U, s - 36U, 3U, fg);
    fill(ox + s / 2U + 4U, oy + 16U, 14U, 14U, fg);
    fill(ox + s / 2U + 8U, oy + 14U, 10U, 10U, bg);
}

static void icon_python(uint16_t ox, uint16_t oy, uint16_t s,
                        uint16_t fg, uint16_t bg)
{
    fill(ox + 10U, oy + 12U, s - 20U, s - 24U, bg);
    frame(ox + 10U, oy + 12U, s - 20U, s - 24U, 2U, fg);
    hk_ui_display_draw_dristy_text_at(ox + 16U, oy + (s / 2U) - 4U, "Py", fg, bg);
}

static void icon_default(uint16_t ox, uint16_t oy, uint16_t s,
                         uint16_t fg, uint16_t bg)
{
    fill(ox + 12U, oy + 12U, s - 24U, s - 24U, bg);
    frame(ox + 12U, oy + 12U, s - 24U, s - 24U, 2U, fg);
    fill(ox + s / 2U - 4U, oy + s / 2U - 4U, 8U, 8U, fg);
}

void dristy_icon_draw(const char *app_id,
                      uint16_t x, uint16_t y, uint16_t box,
                      uint16_t fg, uint16_t bg)
{
    uint16_t pad = box >= 44U ? 3U : 6U;
    uint16_t s = box > pad * 2U ? (uint16_t)(box - pad * 2U) : box;
    uint16_t ox = (uint16_t)(x + (box - s) / 2U);
    uint16_t oy = (uint16_t)(y + (box - s) / 2U);

    if(!app_id)
    {
        icon_default(ox, oy, s, fg, bg);
        return;
    }
    if(strcmp(app_id, "terminal") == 0)
        icon_terminal(ox, oy, s, fg, bg);
    else if(strcmp(app_id, "camera") == 0)
        icon_camera(ox, oy, s, fg, bg);
    else if(strcmp(app_id, "qr_camera") == 0)
        icon_qr(ox, oy, s, fg, bg);
    else if(strcmp(app_id, "face_detect") == 0)
        icon_face(ox, oy, s, fg, bg);
    else if(strcmp(app_id, "apriltag") == 0)
        icon_tag(ox, oy, s, fg, bg);
    else if(strcmp(app_id, "object_detect") == 0)
        icon_object(ox, oy, s, fg, bg);
    else if(strcmp(app_id, "files") == 0)
        icon_files(ox, oy, s, fg, bg);
    else if(strcmp(app_id, "buttons") == 0)
        icon_buttons(ox, oy, s, fg, bg);
    else if(strcmp(app_id, "pong") == 0)
        icon_pong(ox, oy, s, fg, bg);
    else if(strcmp(app_id, "settings") == 0)
        icon_settings(ox, oy, s, fg, bg);
    else if(strcmp(app_id, "sleep") == 0)
        icon_sleep(ox, oy, s, fg, bg);
    else if(strcmp(app_id, "micropython") == 0)
        icon_python(ox, oy, s, fg, bg);
    else
        icon_default(ox, oy, s, fg, bg);
}
