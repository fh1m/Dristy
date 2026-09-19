#include "dristy_distance.h"

#include <math.h>

static dristy_distance_config_t g_config;

void dristy_distance_init(const dristy_distance_config_t *config)
{
    if(config)
        g_config = *config;
    else
    {
        dristy_distance_config_t defaults = DRISTY_DISTANCE_CONFIG_DEFAULT;
        g_config = defaults;
    }
}

void dristy_distance_add_size_ref(uint8_t class_id,
                                  float real_width_mm,
                                  float real_height_mm)
{
    if(g_config.num_size_refs >= DRISTY_MAX_SIZE_REFS)
        return;

    dristy_size_reference_t *ref = &g_config.size_refs[g_config.num_size_refs];
    ref->class_id = class_id;
    ref->real_width_mm = real_width_mm;
    ref->real_height_mm = real_height_mm > 0 ? real_height_mm : real_width_mm;
    g_config.num_size_refs++;
}

float dristy_distance_from_bbox(uint8_t class_id,
                                uint16_t bbox_w, uint16_t bbox_h)
{
    /* Find matching size reference */
    const dristy_size_reference_t *ref = NULL;
    const dristy_size_reference_t *default_ref = NULL;

    for(uint8_t i = 0; i < g_config.num_size_refs; i++)
    {
        if(g_config.size_refs[i].class_id == class_id)
        {
            ref = &g_config.size_refs[i];
            break;
        }
        if(g_config.size_refs[i].class_id == 0xFF)
            default_ref = &g_config.size_refs[i];
    }

    if(!ref) ref = default_ref;
    if(!ref || bbox_w == 0 || bbox_h == 0)
        return 0;

    /* distance = (focal_length × real_size) / pixel_size
     *
     * Use whichever dimension gives a more reliable estimate:
     * - Width-based: less affected by partial occlusion from top/bottom
     * - Height-based: less affected by rotation
     * Average both for robustness. */
    float dist_w = (g_config.focal_length_px * ref->real_width_mm) / bbox_w;
    float dist_h = (g_config.focal_length_px * ref->real_height_mm) / bbox_h;

    /* Weighted average: prefer the dimension closer to expected aspect ratio */
    float expected_aspect = ref->real_width_mm / ref->real_height_mm;
    float actual_aspect = (float)bbox_w / bbox_h;
    float aspect_err = fabsf(actual_aspect - expected_aspect) / expected_aspect;

    /* If aspect ratio is way off, one dimension is likely occluded */
    if(aspect_err > 0.5f)
    {
        /* Use the dimension that's more consistent */
        return (actual_aspect > expected_aspect) ? dist_h : dist_w;
    }

    return (dist_w + dist_h) / 2.0f;
}

float dristy_distance_from_ground(uint16_t pixel_y, uint16_t frame_height)
{
    if(g_config.camera_height_mm <= 0 || frame_height == 0)
        return 0;

    /* Convert pixel Y to angle from camera axis:
     * angle = atan((pixel_y - cy) / fy)
     * where cy = frame_height/2, fy = focal_length */
    float cy = frame_height / 2.0f;
    float angle_from_centre = atanf((pixel_y - cy) / g_config.focal_length_px);

    /* Total angle from horizon = camera_pitch + angle_from_centre
     * distance = camera_height / tan(total_angle) */
    float pitch_rad = g_config.camera_pitch_deg * 3.14159265f / 180.0f;
    float total_angle = pitch_rad + angle_from_centre;

    if(total_angle <= 0.01f)
        return 0; /* object is above horizon, can't estimate ground distance */

    return g_config.camera_height_mm / tanf(total_angle);
}

uint16_t dristy_distance_ttc(uint16_t size_prev, uint16_t size_curr,
                             uint32_t dt_ms)
{
    if(size_prev == 0 || size_curr == 0 || dt_ms == 0)
        return 0;

    /* TTC = size / (d(size)/dt)
     * If size is growing → object approaching → TTC is positive
     * If size is shrinking → receding → return max */
    int32_t delta = (int32_t)size_curr - (int32_t)size_prev;

    if(delta <= 0)
        return 0xFFFF; /* receding or stationary */

    /* TTC = size_curr / (delta / dt_ms) = size_curr * dt_ms / delta */
    uint32_t ttc = (uint32_t)size_curr * dt_ms / (uint32_t)delta;

    return ttc > 0xFFFF ? 0xFFFF : (uint16_t)ttc;
}
