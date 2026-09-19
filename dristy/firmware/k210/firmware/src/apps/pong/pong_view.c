#include "pong_view.h"

#include <stdio.h>

#include "../../ui/display_binding.h"
#include "../../ui/hk_ui.h"

#define PONG_DIRTY_REGION_MAX HK_UI_DISPLAY_FRAME_MAX_DIRTY_RECTS

typedef struct
{
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} pong_rect_t;

typedef struct
{
    pong_rect_t regions[PONG_DIRTY_REGION_MAX];
    uint8_t count;
} pong_dirty_list_t;

static uint8_t pong_rect_intersection(pong_rect_t first, pong_rect_t second,
                                      pong_rect_t *result)
{
    int16_t left = first.x > second.x ? first.x : second.x;
    int16_t top = first.y > second.y ? first.y : second.y;
    int16_t right_first = first.x + first.w;
    int16_t right_second = second.x + second.w;
    int16_t bottom_first = first.y + first.h;
    int16_t bottom_second = second.y + second.h;
    int16_t right = right_first < right_second ? right_first : right_second;
    int16_t bottom = bottom_first < bottom_second ? bottom_first : bottom_second;

    if(left >= right || top >= bottom)
        return 0;
    result->x = left;
    result->y = top;
    result->w = right - left;
    result->h = bottom - top;
    return 1;
}

static uint8_t pong_rect_clip_screen(pong_rect_t *rect)
{
    const pong_rect_t screen = {0, 0, HK_DISPLAY_REQUIRED_WIDTH, HK_DISPLAY_REQUIRED_HEIGHT};
    pong_rect_t clipped;

    if(!pong_rect_intersection(*rect, screen, &clipped))
        return 0;
    *rect = clipped;
    return 1;
}

static uint8_t pong_rects_touch(pong_rect_t first, pong_rect_t second)
{
    return first.x <= second.x + second.w && second.x <= first.x + first.w &&
           first.y <= second.y + second.h && second.y <= first.y + first.h;
}

static pong_rect_t pong_rect_union(pong_rect_t first, pong_rect_t second)
{
    pong_rect_t result;
    int16_t right_first = first.x + first.w;
    int16_t right_second = second.x + second.w;
    int16_t bottom_first = first.y + first.h;
    int16_t bottom_second = second.y + second.h;
    int16_t right;
    int16_t bottom;

    result.x = first.x < second.x ? first.x : second.x;
    result.y = first.y < second.y ? first.y : second.y;
    right = right_first > right_second ? right_first : right_second;
    bottom = bottom_first > bottom_second ? bottom_first : bottom_second;
    result.w = right - result.x;
    result.h = bottom - result.y;
    return result;
}

static uint32_t pong_rect_area(pong_rect_t rect)
{
    return (uint32_t)rect.w * (uint32_t)rect.h;
}

static void pong_dirty_add(pong_dirty_list_t *dirty, pong_rect_t region)
{
    uint8_t index = 0;

    if(!pong_rect_clip_screen(&region))
        return;
    while(index < dirty->count)
    {
        if(!pong_rects_touch(dirty->regions[index], region))
        {
            index++;
            continue;
        }
        region = pong_rect_union(dirty->regions[index], region);
        dirty->count--;
        dirty->regions[index] = dirty->regions[dirty->count];
        index = 0;
    }
    if(dirty->count < PONG_DIRTY_REGION_MAX)
    {
        dirty->regions[dirty->count] = region;
        dirty->count++;
        return;
    }

    {
        uint8_t best = 0U;
        pong_rect_t best_union = pong_rect_union(dirty->regions[0], region);
        uint32_t best_growth = pong_rect_area(best_union) -
            pong_rect_area(dirty->regions[0]);

        for(uint8_t candidate = 1U; candidate < dirty->count; candidate++)
        {
            pong_rect_t combined = pong_rect_union(
                dirty->regions[candidate], region);
            uint32_t growth = pong_rect_area(combined) -
                pong_rect_area(dirty->regions[candidate]);

            if(growth < best_growth)
            {
                best = candidate;
                best_union = combined;
                best_growth = growth;
            }
        }
        dirty->regions[best] = best_union;
    }
}

static void pong_fill_shape(pong_rect_t shape, const pong_rect_t *clip,
                            uint16_t color)
{
    pong_rect_t visible = shape;

    if(clip && !pong_rect_intersection(visible, *clip, &visible))
        return;
    if(!pong_rect_clip_screen(&visible))
        return;
    hk_ui_display_fill_rect((uint16_t)visible.x, (uint16_t)visible.y,
                  (uint16_t)visible.w, (uint16_t)visible.h, color);
}

static void pong_view_draw_title(pong_view_state_t state)
{
    char title[24];

    snprintf(title, sizeof(title), "YOU  %u : %u  CPU", state.player_score, state.ai_score);
    hk_ui_display_fill_rect(MENU_LINE,
                  MENU_LINE,
                  HK_DISPLAY_REQUIRED_WIDTH - MENU_LINE * 2,
                  MENU_BAR_H - MENU_LINE * 2,
                  COLOR_BLACK);
    hk_ui_display_draw_text_centered(3, title, COLOR_TERM_GREEN, COLOR_BLACK);
}

static void pong_view_draw_chrome(void)
{
    hk_ui_display_fill_rect(0, 0, HK_DISPLAY_REQUIRED_WIDTH, HK_DISPLAY_REQUIRED_HEIGHT, COLOR_BLACK);
    hk_ui_display_draw_rect(0, 0, HK_DISPLAY_REQUIRED_WIDTH, HK_DISPLAY_REQUIRED_HEIGHT, MENU_LINE, COLOR_TERM_GREEN);
    hk_ui_display_fill_rect(0, MENU_BAR_H - MENU_LINE, HK_DISPLAY_REQUIRED_WIDTH, MENU_LINE, COLOR_TERM_GREEN);
}

static void pong_view_draw_chrome_clipped(const pong_rect_t *clip)
{
    const pong_rect_t edges[] = {
        {0, 0, HK_DISPLAY_REQUIRED_WIDTH, MENU_LINE},
        {0, HK_DISPLAY_REQUIRED_HEIGHT - MENU_LINE, HK_DISPLAY_REQUIRED_WIDTH, MENU_LINE},
        {0, 0, MENU_LINE, HK_DISPLAY_REQUIRED_HEIGHT},
        {HK_DISPLAY_REQUIRED_WIDTH - MENU_LINE, 0, MENU_LINE, HK_DISPLAY_REQUIRED_HEIGHT},
        {0, MENU_BAR_H - MENU_LINE, HK_DISPLAY_REQUIRED_WIDTH, MENU_LINE},
    };

    for(uint8_t i = 0; i < (uint8_t)(sizeof(edges) / sizeof(edges[0])); i++)
        pong_fill_shape(edges[i], clip, COLOR_TERM_GREEN);
}

static void pong_view_draw_border(void)
{
    hk_ui_display_draw_rect(PONG_FIELD_X, PONG_FIELD_Y, PONG_FIELD_W, PONG_FIELD_H,
                  MENU_LINE, COLOR_TERM_GREEN);
}

static void pong_view_draw_border_clipped(const pong_rect_t *clip)
{
    const pong_rect_t edges[] = {
        {PONG_FIELD_X, PONG_FIELD_Y, PONG_FIELD_W, MENU_LINE},
        {PONG_FIELD_X, PONG_FIELD_Y + PONG_FIELD_H - MENU_LINE,
         PONG_FIELD_W, MENU_LINE},
        {PONG_FIELD_X, PONG_FIELD_Y, MENU_LINE, PONG_FIELD_H},
        {PONG_FIELD_X + PONG_FIELD_W - MENU_LINE, PONG_FIELD_Y,
         MENU_LINE, PONG_FIELD_H},
    };

    for(uint8_t i = 0; i < (uint8_t)(sizeof(edges) / sizeof(edges[0])); i++)
        pong_fill_shape(edges[i], clip, COLOR_TERM_GREEN);
}

static void pong_view_draw_midline_clipped(const pong_rect_t *clip)
{
    const uint16_t y = PONG_FIELD_Y + PONG_FIELD_H / 2;
    const uint16_t left = PONG_FIELD_X + MENU_LINE;
    const uint16_t right = PONG_FIELD_X + PONG_FIELD_W - MENU_LINE;
    const uint16_t dash = 8;
    const uint16_t step = 16;
    const uint16_t count = (right - left) / step;
    const uint16_t pattern_width = (count - 1) * step + dash;
    const uint16_t start = left + (right - left - pattern_width) / 2;

    for(uint16_t x = start; x + dash <= right; x += step)
    {
        const pong_rect_t segment = {(int16_t)x, (int16_t)(y - 1), dash, 2};

        pong_fill_shape(segment, clip, COLOR_TERM_GREEN);
    }
}

static void pong_view_draw_midline(void)
{
    pong_view_draw_midline_clipped(NULL);
}

static pong_rect_t pong_view_trail_rect(pong_view_state_t state, uint8_t index)
{
    int16_t size = index == 0 ? PONG_BALL_SIZE - 2 : PONG_BALL_SIZE - 4;
    int16_t inset = (PONG_BALL_SIZE - size) / 2;
    pong_rect_t result = {
        state.trail_x[index] + inset,
        state.trail_y[index] + inset,
        size,
        size,
    };

    return result;
}

static void pong_view_draw_trail(pong_view_state_t state, uint16_t color,
                                 const pong_rect_t *clip)
{
    for(uint8_t i = 0; i < state.trail_count && i < PONG_TRAIL_LENGTH; i++)
        pong_fill_shape(pong_view_trail_rect(state, i), clip, color);
}

static void pong_view_draw_objects(pong_view_state_t state, uint16_t paddle_color,
                                   uint16_t ball_color, const pong_rect_t *clip)
{
    const pong_rect_t player = {state.player_x, PONG_PLAYER_Y,
                                PONG_PADDLE_W, PONG_PADDLE_H};
    const pong_rect_t ai = {state.ai_x, PONG_AI_Y,
                            PONG_PADDLE_W, PONG_PADDLE_H};
    const pong_rect_t ball = {state.ball_x, state.ball_y,
                              PONG_BALL_SIZE, PONG_BALL_SIZE};

    pong_fill_shape(player, clip, paddle_color);
    pong_fill_shape(ai, clip, paddle_color);
    pong_fill_shape(ball, clip, ball_color);
}

static pong_rect_t pong_view_flash_rect(pong_view_state_t state)
{
    const pong_rect_t result = {state.flash_x - 5, state.flash_y - 5, 11, 11};

    return result;
}

static void pong_view_draw_flash(pong_view_state_t state, uint16_t color,
                                 const pong_rect_t *clip)
{
    const pong_rect_t horizontal = {state.flash_x - 5, state.flash_y - 1, 11, 3};
    const pong_rect_t vertical = {state.flash_x - 1, state.flash_y - 5, 3, 11};

    if(state.flash_ticks == 0)
        return;
    pong_fill_shape(horizontal, clip, color);
    pong_fill_shape(vertical, clip, color);
}

static void pong_view_draw_dynamic(pong_view_state_t state,
                                   const pong_rect_t *clip)
{
    pong_view_draw_trail(state, PONG_TRAIL_COLOR, clip);
    pong_view_draw_objects(state, COLOR_TERM_GREEN, PONG_BALL_COLOR, clip);
    pong_view_draw_flash(state, PONG_FLASH_COLOR, clip);
}

static uint8_t pong_view_trail_changed(pong_view_state_t previous,
                                       pong_view_state_t current)
{
    if(previous.trail_count != current.trail_count)
        return 1;

    for(uint8_t i = 0; i < previous.trail_count && i < PONG_TRAIL_LENGTH; i++)
    {
        if(previous.trail_x[i] != current.trail_x[i] ||
           previous.trail_y[i] != current.trail_y[i])
            return 1;
    }
    return 0;
}

static uint8_t pong_view_flash_changed(pong_view_state_t previous,
                                       pong_view_state_t current)
{
    uint8_t previous_visible = previous.flash_ticks > 0;
    uint8_t current_visible = current.flash_ticks > 0;

    return previous_visible != current_visible ||
           (previous_visible && (previous.flash_x != current.flash_x ||
                                 previous.flash_y != current.flash_y));
}

static void pong_dirty_add_trail(pong_dirty_list_t *dirty,
                                 pong_view_state_t state)
{
    for(uint8_t i = 0; i < state.trail_count && i < PONG_TRAIL_LENGTH; i++)
        pong_dirty_add(dirty, pong_view_trail_rect(state, i));
}

static pong_dirty_list_t pong_view_dirty_regions(pong_view_state_t previous,
                                                 pong_view_state_t current)
{
    pong_dirty_list_t dirty = {{{0, 0, 0, 0}}, 0};

    if(previous.player_x != current.player_x)
    {
        pong_dirty_add(&dirty, (pong_rect_t){previous.player_x, PONG_PLAYER_Y,
                                             PONG_PADDLE_W, PONG_PADDLE_H});
        pong_dirty_add(&dirty, (pong_rect_t){current.player_x, PONG_PLAYER_Y,
                                             PONG_PADDLE_W, PONG_PADDLE_H});
    }
    if(previous.ai_x != current.ai_x)
    {
        pong_dirty_add(&dirty, (pong_rect_t){previous.ai_x, PONG_AI_Y,
                                             PONG_PADDLE_W, PONG_PADDLE_H});
        pong_dirty_add(&dirty, (pong_rect_t){current.ai_x, PONG_AI_Y,
                                             PONG_PADDLE_W, PONG_PADDLE_H});
    }
    if(previous.ball_x != current.ball_x || previous.ball_y != current.ball_y)
    {
        pong_dirty_add(&dirty, (pong_rect_t){previous.ball_x, previous.ball_y,
                                             PONG_BALL_SIZE, PONG_BALL_SIZE});
        pong_dirty_add(&dirty, (pong_rect_t){current.ball_x, current.ball_y,
                                             PONG_BALL_SIZE, PONG_BALL_SIZE});
    }
    if(pong_view_trail_changed(previous, current))
    {
        pong_dirty_add_trail(&dirty, previous);
        pong_dirty_add_trail(&dirty, current);
    }
    if(pong_view_flash_changed(previous, current))
    {
        if(previous.flash_ticks > 0)
            pong_dirty_add(&dirty, pong_view_flash_rect(previous));
        if(current.flash_ticks > 0)
            pong_dirty_add(&dirty, pong_view_flash_rect(current));
    }
    return dirty;
}

static void pong_view_restore_region(pong_view_state_t current,
                                     pong_rect_t region)
{
    pong_fill_shape(region, NULL, COLOR_BLACK);
    pong_view_draw_chrome_clipped(&region);
    pong_view_draw_border_clipped(&region);
    pong_view_draw_midline_clipped(&region);
    pong_view_draw_dynamic(current, &region);
}

void pong_view_render_initial(pong_view_state_t state)
{
    hk_ui_display_surface_t frame;

    if(!hk_ui_display_frame_acquire(&frame))
        return;
    pong_view_draw_chrome();
    pong_view_draw_border();
    pong_view_draw_midline();
    pong_view_draw_title(state);
    pong_view_draw_dynamic(state, NULL);
    if(!hk_ui_display_frame_present(frame.lease_id))
        hk_ui_display_frame_cancel(frame.lease_id);
}

void pong_view_render_score(pong_view_state_t state)
{
    hk_ui_display_surface_t frame;
    const hk_ui_display_rect_t title = {
        MENU_LINE, MENU_LINE,
        HK_DISPLAY_REQUIRED_WIDTH - MENU_LINE * 2,
        MENU_BAR_H - MENU_LINE * 2,
    };

    if(!hk_ui_display_frame_acquire(&frame))
        return;
    pong_view_draw_title(state);
    if(!hk_ui_display_frame_present_regions(frame.lease_id, &title, 1U))
        hk_ui_display_frame_cancel(frame.lease_id);
}

void pong_view_render_frame(pong_view_state_t previous, pong_view_state_t current)
{
    pong_dirty_list_t dirty = pong_view_dirty_regions(previous, current);
    hk_ui_display_rect_t regions[PONG_DIRTY_REGION_MAX];
    hk_ui_display_surface_t frame;

    if(dirty.count == 0U || !hk_ui_display_frame_acquire(&frame))
        return;

    for(uint8_t i = 0; i < dirty.count; i++)
    {
        pong_view_restore_region(current, dirty.regions[i]);
        regions[i] = (hk_ui_display_rect_t){
            (uint16_t)dirty.regions[i].x,
            (uint16_t)dirty.regions[i].y,
            (uint16_t)dirty.regions[i].w,
            (uint16_t)dirty.regions[i].h,
        };
    }
    if(!hk_ui_display_frame_present_regions(
           frame.lease_id, regions, dirty.count))
        hk_ui_display_frame_cancel(frame.lease_id);
}

void pong_view_draw_icon(uint16_t x, uint16_t y, uint16_t color, uint16_t bg)
{
    hk_ui_display_surface_t frame;
    const hk_ui_display_rect_t icon = {x, y, 60U, 60U};

    (void)bg;
    if(!hk_ui_display_frame_acquire(&frame))
        return;
    hk_ui_display_fill_rect(x + 8, y + 10, 4, 40, color);
    hk_ui_display_fill_rect(x + 48, y + 10, 4, 40, color);
    hk_ui_display_fill_rect(x + 27, y + 27, 6, 6, color);
    hk_ui_display_fill_rect(x + 18, y + 16, 2, 2, color);
    hk_ui_display_fill_rect(x + 40, y + 42, 2, 2, color);
    if(!hk_ui_display_frame_present_regions(frame.lease_id, &icon, 1U))
        hk_ui_display_frame_cancel(frame.lease_id);
}
