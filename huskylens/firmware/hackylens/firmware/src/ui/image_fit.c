#include "hk_ui.h"

#include "../config/display_config.h"

void image_fit_viewport(uint16_t src_w, uint16_t src_h, uint16_t *dst_x, uint16_t *dst_y, uint16_t *dst_w, uint16_t *dst_h)
{
    uint32_t fit_w = HK_DISPLAY_REQUIRED_WIDTH;
    uint32_t fit_h = HK_DISPLAY_REQUIRED_HEIGHT;

    if(src_w == 0 || src_h == 0)
    {
        *dst_x = 0;
        *dst_y = 0;
        *dst_w = HK_DISPLAY_REQUIRED_WIDTH;
        *dst_h = HK_DISPLAY_REQUIRED_HEIGHT;
        return;
    }

    if((uint32_t)src_w * HK_DISPLAY_REQUIRED_HEIGHT > (uint32_t)src_h * HK_DISPLAY_REQUIRED_WIDTH)
        fit_h = ((uint32_t)src_h * HK_DISPLAY_REQUIRED_WIDTH + src_w / 2U) / src_w;
    else
        fit_w = ((uint32_t)src_w * HK_DISPLAY_REQUIRED_HEIGHT + src_h / 2U) / src_h;

    if(fit_w == 0)
        fit_w = 1;
    if(fit_h == 0)
        fit_h = 1;
    if(fit_w > HK_DISPLAY_REQUIRED_WIDTH)
        fit_w = HK_DISPLAY_REQUIRED_WIDTH;
    if(fit_h > HK_DISPLAY_REQUIRED_HEIGHT)
        fit_h = HK_DISPLAY_REQUIRED_HEIGHT;

    *dst_w = (uint16_t)fit_w;
    *dst_h = (uint16_t)fit_h;
    *dst_x = (uint16_t)((HK_DISPLAY_REQUIRED_WIDTH - *dst_w) / 2U);
    *dst_y = (uint16_t)((HK_DISPLAY_REQUIRED_HEIGHT - *dst_h) / 2U);
}
