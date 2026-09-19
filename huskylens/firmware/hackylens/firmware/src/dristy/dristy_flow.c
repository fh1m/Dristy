#include "dristy_flow.h"

#include <string.h>

#define FLOW_PATCH 16

static uint8_t g_prev[FLOW_PATCH * FLOW_PATCH];
static uint8_t g_have_prev;

void dristy_flow_init(void)
{
    g_have_prev = 0U;
}

void dristy_flow_process_gray(const uint8_t *gray, uint16_t w, uint16_t h,
                              dristy_result_snapshot_t *snap)
{
    int16_t cx = (int16_t)(w / 2U);
    int16_t cy = (int16_t)(h / 2U);
    int16_t ox = cx - FLOW_PATCH / 2;
    int16_t oy = cy - FLOW_PATCH / 2;
    uint8_t cur[FLOW_PATCH * FLOW_PATCH];
    int32_t best_sad = 0x7FFFFFFF;
    int16_t best_dx = 0;
    int16_t best_dy = 0;

    if(!gray || !snap || w < 32U || h < 32U)
        return;

    for(int16_t dy = 0; dy < FLOW_PATCH; dy++)
    {
        for(int16_t dx = 0; dx < FLOW_PATCH; dx++)
        {
            cur[(uint32_t)dy * FLOW_PATCH + (uint32_t)dx] =
                gray[(uint32_t)(oy + dy) * w + (uint32_t)(ox + dx)];
        }
    }

    if(!g_have_prev)
    {
        memcpy(g_prev, cur, sizeof(g_prev));
        g_have_prev = 1U;
        return;
    }

    for(int16_t try_dy = -4; try_dy <= 4; try_dy++)
    {
        for(int16_t try_dx = -4; try_dx <= 4; try_dx++)
        {
            int32_t sad = 0;
            for(int16_t dy = 0; dy < FLOW_PATCH; dy++)
            {
                for(int16_t dx = 0; dx < FLOW_PATCH; dx++)
                {
                    int16_t px = ox + dx + try_dx;
                    int16_t py = oy + dy + try_dy;
                    uint8_t v = 0U;
                    if(px >= 0 && py >= 0 && px < (int16_t)w && py < (int16_t)h)
                        v = gray[(uint32_t)py * w + (uint32_t)px];
                    sad += (int32_t)(cur[(uint32_t)dy * FLOW_PATCH + (uint32_t)dx] > v ?
                                       cur[(uint32_t)dy * FLOW_PATCH + (uint32_t)dx] - v :
                                       v - cur[(uint32_t)dy * FLOW_PATCH + (uint32_t)dx]);
                }
            }
            if(sad < best_sad)
            {
                best_sad = sad;
                best_dx = try_dx;
                best_dy = try_dy;
            }
        }
    }

    memcpy(g_prev, cur, sizeof(g_prev));
    snap->flow_count = 1U;
    snap->flow[0].roi_cx = dristy_pixel_to_norm_x(cx, w);
    snap->flow[0].roi_cy = dristy_pixel_to_norm_y(cy, h);
    snap->flow[0].dx_x100 = (int16_t)(best_dx * 100);
    snap->flow[0].dy_x100 = (int16_t)(best_dy * 100);
    snap->flow[0].response = (uint16_t)(best_sad < 5000 ? 800U : 200U);
}
