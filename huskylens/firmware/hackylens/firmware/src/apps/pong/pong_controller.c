#include "pong_controller.h"

#include <stdio.h>

#include <hackylens/capability/time.h>

#include "../../config/display_config.h"
#include "../../config/input_config.h"
#include "../../core/hk_back_exit.h"
#include "../../core/hk_menu.h"
#include "../../core/hk_screen.h"
#include "../../core/hk_capability_client.h"
#include "pong_config.h"
#include "pong_view.h"

static int16_t g_pong_player_x;
static int16_t g_pong_ai_x;
static int16_t g_pong_ai_target_x;
static int16_t g_pong_ball_x;
static int16_t g_pong_ball_y;
static int16_t g_pong_ball_dx;
static int16_t g_pong_ball_dy;
static int16_t g_pong_player_dx;
static int16_t g_pong_ai_dx;
static int16_t g_pong_trail_x[PONG_TRAIL_LENGTH];
static int16_t g_pong_trail_y[PONG_TRAIL_LENGTH];
static int16_t g_pong_flash_x;
static int16_t g_pong_flash_y;
static uint8_t g_pong_player_score;
static uint8_t g_pong_ai_score;
static uint8_t g_pong_rally_hits;
static uint8_t g_pong_trail_count;
static uint8_t g_pong_flash_ticks;
static uint8_t g_pong_serve_ticks;
static uint8_t g_pong_serve_index;
static uint8_t g_pong_ai_reaction_ticks;
static uint8_t g_pong_ai_noise;
static int8_t g_pong_serve_direction;
static uint64_t g_pong_last_tick_us;
static uint64_t g_pong_accumulator_us;
static hk_time_t s_pong_time;
static hk_owner_t s_pong_time_owner;

static uint64_t pong_time_now_us(void)
{
    hk_capability_request_t request = HK_TIME_REQUEST_0_1_INIT;
    hk_owner_t owner = capability_client_current_owner();
    uint64_t value = 0U;

    request.required_features = HK_TIME_FEATURE_MONOTONIC_US;
    if(hk_owner_is_zero(owner))
        return 0U;
    if(owner.slot != s_pong_time_owner.slot ||
       owner.generation != s_pong_time_owner.generation ||
       hk_lease_is_zero(&s_pong_time.lease))
    {
        s_pong_time.lease = HK_LEASE_NONE;
        s_pong_time_owner = owner;
        if(hk_time_acquire(owner, &request, &s_pong_time) != HK_OK)
            return 0U;
    }
    if(hk_time_now_us(owner, &s_pong_time, &value) != HK_OK)
        return 0U;
    return value;
}

static int16_t pong_clamp(int16_t value, int16_t minimum, int16_t maximum)
{
    if(value < minimum)
        return minimum;
    if(value > maximum)
        return maximum;
    return value;
}

static int16_t pong_paddle_min_x(void)
{
    return PONG_FIELD_X + MENU_LINE;
}

static int16_t pong_paddle_max_x(void)
{
    return PONG_FIELD_X + PONG_FIELD_W - MENU_LINE - PONG_PADDLE_W;
}

static int16_t pong_ball_min_x(void)
{
    return PONG_FIELD_X + MENU_LINE;
}

static int16_t pong_ball_max_x(void)
{
    return PONG_FIELD_X + PONG_FIELD_W - MENU_LINE - PONG_BALL_SIZE;
}

static pong_view_state_t pong_view_state(void)
{
    pong_view_state_t state = {
        .player_x = g_pong_player_x,
        .ai_x = g_pong_ai_x,
        .ball_x = g_pong_ball_x,
        .ball_y = g_pong_ball_y,
        .flash_x = g_pong_flash_x,
        .flash_y = g_pong_flash_y,
        .player_score = g_pong_player_score,
        .ai_score = g_pong_ai_score,
        .trail_count = g_pong_trail_count,
        .flash_ticks = g_pong_flash_ticks,
    };

    for(uint8_t i = 0; i < PONG_TRAIL_LENGTH; i++)
    {
        state.trail_x[i] = g_pong_trail_x[i];
        state.trail_y[i] = g_pong_trail_y[i];
    }
    return state;
}

static void pong_clear_effects(void)
{
    g_pong_trail_count = 0;
    g_pong_flash_ticks = 0;
    for(uint8_t i = 0; i < PONG_TRAIL_LENGTH; i++)
    {
        g_pong_trail_x[i] = g_pong_ball_x;
        g_pong_trail_y[i] = g_pong_ball_y;
    }
}

static void pong_begin_serve(int8_t direction)
{
    g_pong_ball_x = PONG_FIELD_X + (PONG_FIELD_W - PONG_BALL_SIZE) / 2;
    g_pong_ball_y = PONG_FIELD_Y + (PONG_FIELD_H - PONG_BALL_SIZE) / 2;
    g_pong_ball_dx = 0;
    g_pong_ball_dy = 0;
    g_pong_serve_direction = direction < 0 ? -1 : 1;
    g_pong_serve_ticks = PONG_SERVE_DELAY_TICKS;
    g_pong_rally_hits = 0;
    g_pong_ai_target_x = (HK_DISPLAY_REQUIRED_WIDTH - PONG_PADDLE_W) / 2;
    g_pong_ai_reaction_ticks = PONG_AI_REACTION_TICKS;
    pong_clear_effects();
}

static void pong_launch_serve(void)
{
    static const int8_t serve_dx[] = {
        -PONG_BALL_START_DX,
        PONG_BALL_START_DX,
        -(PONG_BALL_START_DX - 1),
        PONG_BALL_START_DX - 1,
    };

    g_pong_ball_dx = serve_dx[g_pong_serve_index % (sizeof(serve_dx) / sizeof(serve_dx[0]))];
    g_pong_ball_dy = g_pong_serve_direction * PONG_BALL_START_DY;
    g_pong_serve_index++;
}

static void pong_reset(void)
{
    g_pong_player_x = (HK_DISPLAY_REQUIRED_WIDTH - PONG_PADDLE_W) / 2;
    g_pong_ai_x = (HK_DISPLAY_REQUIRED_WIDTH - PONG_PADDLE_W) / 2;
    g_pong_player_dx = 0;
    g_pong_ai_dx = 0;
    g_pong_player_score = 0;
    g_pong_ai_score = 0;
    g_pong_serve_index = 0;
    g_pong_ai_noise = 0xA5;
    g_pong_last_tick_us = pong_time_now_us();
    g_pong_accumulator_us = 0;
    pong_begin_serve(1);
}

static uint8_t pong_intersects(int16_t bx, int16_t by, int16_t px, int16_t py)
{
    return bx + PONG_BALL_SIZE >= px && bx <= px + PONG_PADDLE_W &&
           by + PONG_BALL_SIZE >= py && by <= py + PONG_PADDLE_H;
}

static uint8_t pong_next_noise(void)
{
    uint8_t value = g_pong_ai_noise;

    value ^= (uint8_t)(value << 3);
    value ^= (uint8_t)(value >> 5);
    value ^= (uint8_t)(value << 1);
    if(value == 0)
        value = 0xA5;
    g_pong_ai_noise = value;
    return value;
}

static int16_t pong_reflect_ball_x(int32_t projected_x)
{
    int32_t left = pong_ball_min_x();
    int32_t span = pong_ball_max_x() - left;
    int32_t period = span * 2;
    int32_t relative = (projected_x - left) % period;

    if(relative < 0)
        relative += period;
    if(relative > span)
        relative = period - relative;
    return (int16_t)(left + relative);
}

static void pong_update_ai_target(void)
{
    int16_t target = (HK_DISPLAY_REQUIRED_WIDTH - PONG_PADDLE_W) / 2;

    if(g_pong_ball_dy < 0)
    {
        int16_t distance = g_pong_ball_y - (PONG_AI_Y + PONG_PADDLE_H);
        int16_t vertical_speed = -g_pong_ball_dy;
        int16_t travel_ticks;
        int32_t projected_x;
        int16_t error;

        if(distance < 0)
            distance = 0;
        travel_ticks = (distance + vertical_speed - 1) / vertical_speed;
        projected_x = g_pong_ball_x + (int32_t)g_pong_ball_dx * travel_ticks;
        error = (int16_t)(pong_next_noise() % (PONG_AI_ERROR_PX * 2 + 1)) - PONG_AI_ERROR_PX;
        target = pong_reflect_ball_x(projected_x) + PONG_BALL_SIZE / 2 - PONG_PADDLE_W / 2 + error;
    }
    g_pong_ai_target_x = pong_clamp(target, pong_paddle_min_x(), pong_paddle_max_x());
}

static void pong_update_player(const hk_input_snapshot_t *input)
{
    int16_t previous_x = g_pong_player_x;

    if(input->state & BUTTON_LEFT)
        g_pong_player_x -= PONG_PADDLE_SPEED;
    if(input->state & BUTTON_RIGHT)
        g_pong_player_x += PONG_PADDLE_SPEED;
    g_pong_player_x = pong_clamp(g_pong_player_x, pong_paddle_min_x(), pong_paddle_max_x());
    g_pong_player_dx = g_pong_player_x - previous_x;
}

static void pong_update_ai(void)
{
    int16_t previous_x = g_pong_ai_x;
    int16_t distance;

    if(g_pong_ai_reaction_ticks > 0)
        g_pong_ai_reaction_ticks--;
    else
    {
        pong_update_ai_target();
        g_pong_ai_reaction_ticks = PONG_AI_REACTION_TICKS - 1;
    }

    distance = g_pong_ai_target_x - g_pong_ai_x;
    if(distance < -PONG_AI_DEAD_ZONE)
        g_pong_ai_x -= distance < -PONG_AI_SPEED ? PONG_AI_SPEED : -distance;
    else if(distance > PONG_AI_DEAD_ZONE)
        g_pong_ai_x += distance > PONG_AI_SPEED ? PONG_AI_SPEED : distance;
    g_pong_ai_x = pong_clamp(g_pong_ai_x, pong_paddle_min_x(), pong_paddle_max_x());
    g_pong_ai_dx = g_pong_ai_x - previous_x;
}

static void pong_push_trail(void)
{
    for(uint8_t i = PONG_TRAIL_LENGTH - 1; i > 0; i--)
    {
        g_pong_trail_x[i] = g_pong_trail_x[i - 1];
        g_pong_trail_y[i] = g_pong_trail_y[i - 1];
    }
    g_pong_trail_x[0] = g_pong_ball_x;
    g_pong_trail_y[0] = g_pong_ball_y;
    if(g_pong_trail_count < PONG_TRAIL_LENGTH)
        g_pong_trail_count++;
}

static int16_t pong_rally_vertical_speed(void)
{
    int16_t speed = PONG_BALL_START_DY + g_pong_rally_hits / PONG_RALLY_SPEEDUP_EVERY;

    return speed > PONG_BALL_MAX_DY ? PONG_BALL_MAX_DY : speed;
}

static int16_t pong_bounce_horizontal(int16_t paddle_x, int16_t paddle_dx)
{
    int16_t ball_center = g_pong_ball_x + PONG_BALL_SIZE / 2;
    int16_t paddle_center = paddle_x + PONG_PADDLE_W / 2;
    int16_t offset = ball_center - paddle_center;
    int16_t result = (int16_t)(((int32_t)offset * PONG_BALL_MAX_DX) / (PONG_PADDLE_W / 2));

    result += paddle_dx / 2;
    result = pong_clamp(result, -PONG_BALL_MAX_DX, PONG_BALL_MAX_DX);
    if(result == 0)
        result = g_pong_ball_dx < 0 ? -1 : 1;
    return result;
}

static void pong_flash_hit(int16_t paddle_y)
{
    g_pong_flash_x = g_pong_ball_x + PONG_BALL_SIZE / 2;
    g_pong_flash_y = paddle_y + PONG_PADDLE_H / 2;
    g_pong_flash_ticks = PONG_FLASH_TICKS;
}

static void pong_advance_ball(void)
{
    pong_push_trail();
    g_pong_ball_x += g_pong_ball_dx;
    g_pong_ball_y += g_pong_ball_dy;

    if(g_pong_ball_x <= pong_ball_min_x())
    {
        g_pong_ball_x = pong_ball_min_x();
        if(g_pong_ball_dx < 0)
            g_pong_ball_dx = -g_pong_ball_dx;
    }
    else if(g_pong_ball_x >= pong_ball_max_x())
    {
        g_pong_ball_x = pong_ball_max_x();
        if(g_pong_ball_dx > 0)
            g_pong_ball_dx = -g_pong_ball_dx;
    }

    if(g_pong_ball_dy > 0 && pong_intersects(g_pong_ball_x, g_pong_ball_y, g_pong_player_x, PONG_PLAYER_Y))
    {
        g_pong_ball_y = PONG_PLAYER_Y - PONG_BALL_SIZE;
        if(g_pong_rally_hits < UINT8_MAX)
            g_pong_rally_hits++;
        g_pong_ball_dx = pong_bounce_horizontal(g_pong_player_x, g_pong_player_dx);
        g_pong_ball_dy = -pong_rally_vertical_speed();
        pong_flash_hit(PONG_PLAYER_Y);
    }
    else if(g_pong_ball_dy < 0 && pong_intersects(g_pong_ball_x, g_pong_ball_y, g_pong_ai_x, PONG_AI_Y))
    {
        g_pong_ball_y = PONG_AI_Y + PONG_PADDLE_H;
        if(g_pong_rally_hits < UINT8_MAX)
            g_pong_rally_hits++;
        g_pong_ball_dx = pong_bounce_horizontal(g_pong_ai_x, g_pong_ai_dx);
        g_pong_ball_dy = pong_rally_vertical_speed();
        pong_flash_hit(PONG_AI_Y);
    }
}

static uint8_t pong_update_score(void)
{
    if(g_pong_ball_y + PONG_BALL_SIZE < PONG_FIELD_Y + MENU_LINE)
    {
        if(g_pong_player_score < 99)
            g_pong_player_score++;
        pong_begin_serve(-1);
        return 1;
    }
    if(g_pong_ball_y > PONG_FIELD_Y + PONG_FIELD_H - MENU_LINE)
    {
        if(g_pong_ai_score < 99)
            g_pong_ai_score++;
        pong_begin_serve(1);
        return 1;
    }
    return 0;
}

static uint8_t pong_simulate_step(const hk_input_snapshot_t *input)
{
    uint8_t ball_was_active = g_pong_serve_ticks == 0;

    if(g_pong_flash_ticks > 0)
        g_pong_flash_ticks--;
    pong_update_player(input);

    if(g_pong_serve_ticks > 0)
    {
        g_pong_serve_ticks--;
        if(g_pong_serve_ticks == 0)
            pong_launch_serve();
    }
    pong_update_ai();

    if(!ball_was_active)
        return 0;
    pong_advance_ball();
    return pong_update_score();
}

void pong_controller_enter(const hk_input_snapshot_t *input)
{
    (void)input;
    hk_screen_set(HK_PONG_SCREEN);
    hk_back_exit_set_armed(0);
    pong_reset();
    pong_view_render_initial(pong_view_state());
    printf("[SHELL] screen PONG\r\n");
}

void pong_controller_tick(const hk_input_snapshot_t *input)
{
    static const hk_input_snapshot_t idle_input = {0U, 0U, 0U};
    const uint64_t maximum_accumulator =
        PONG_FIXED_STEP_US * PONG_MAX_CATCH_UP_STEPS;
    pong_view_state_t previous;
    pong_view_state_t current;
    uint64_t now_us = pong_time_now_us();
    uint64_t elapsed_us;
    uint8_t score_changed = 0;
    uint8_t steps = 0;

    if(!input)
        input = &idle_input;
    if(now_us < g_pong_last_tick_us)
    {
        g_pong_last_tick_us = now_us;
        g_pong_accumulator_us = 0;
        return;
    }

    elapsed_us = now_us - g_pong_last_tick_us;
    g_pong_last_tick_us = now_us;
    if(elapsed_us >= maximum_accumulator ||
       g_pong_accumulator_us >= maximum_accumulator - elapsed_us)
        g_pong_accumulator_us = maximum_accumulator;
    else
        g_pong_accumulator_us += elapsed_us;

    if(g_pong_accumulator_us < PONG_FIXED_STEP_US)
        return;

    previous = pong_view_state();
    while(g_pong_accumulator_us >= PONG_FIXED_STEP_US &&
          steps < PONG_MAX_CATCH_UP_STEPS)
    {
        score_changed |= pong_simulate_step(input);
        g_pong_accumulator_us -= PONG_FIXED_STEP_US;
        steps++;
    }

    current = pong_view_state();
    pong_view_render_frame(previous, current);
    if(score_changed)
        pong_view_render_score(current);
}

void pong_controller_handle_buttons(const hk_input_snapshot_t *input)
{
    if(input->pressed & BUTTON_BACK)
    {
        shell_show_menu();
        return;
    }
    if(input->pressed & BUTTON_OK)
    {
        pong_reset();
        pong_view_render_initial(pong_view_state());
        printf("[PONG] reset\r\n");
    }
}

#ifdef PONG_HOST_TESTING
pong_view_state_t pong_controller_test_state(void)
{
    return pong_view_state();
}
#endif
