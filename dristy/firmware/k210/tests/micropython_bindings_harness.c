#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "micropython_binding_test_platform.h"
#include "micropython_capability_bridge.h"
#include "display_stage_private.h"

#define LCD_OVERLAY_COMMAND_MAX 32U
#define LCD_OVERLAY_TEXT_MAX 1024U
#define LCD_OVERLAY_COMMAND_CLEAR 1U
#define LCD_OVERLAY_COMMAND_TEXT 2U
#define LCD_OVERLAY_COMMAND_RECT 3U
#define LCD_OVERLAY_COMMAND_FILL 4U
#define LCD_OVERLAY_PRESENT_OK HK_OK
#define LCD_OVERLAY_PRESENT_CANCELLED HK_ERR_CANCELLED

typedef struct
{
    uint8_t type;
    uint8_t filled;
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint16_t color;
    uint16_t text_offset;
    uint16_t text_length;
} test_display_command_t;

typedef enum
{
    SCHEDULE_TICK_EACH_SLEEP = 0,
    SCHEDULE_TIMEOUT_BEFORE_TICK,
    SCHEDULE_UART_TICK_THEN_TIMEOUT,
    SCHEDULE_UART_DRAIN,
} test_schedule_t;

static uint64_t g_now_us;
static uint32_t g_run_id;
static uint32_t g_sleep_calls;
static uint32_t g_interrupt_poll;
static uint32_t g_interrupt_on_poll;
static uint32_t g_vm_hook_calls;
static uint32_t g_unwind_before_ack;
static uint32_t g_external_suspend_calls;
static uint32_t g_external_resume_calls;
static uint32_t g_illum_restore_calls;
static uint32_t g_rgb_restore_calls;
static uint32_t g_light_writes;
static uint32_t g_uart_init_calls;
static uint32_t g_overlay_acquire_calls;
static uint32_t g_overlay_present_calls;
static uint32_t g_overlay_release_calls;
static uint32_t g_overlay_acquired_run_id;
static uint32_t g_overlay_released_run_id;
static size_t g_overlay_command_count;
static size_t g_overlay_text_length;
static test_display_command_t
    g_overlay_commands[LCD_OVERLAY_COMMAND_MAX];
static uint8_t g_overlay_text[LCD_OVERLAY_TEXT_MAX];
static hk_result_t g_overlay_present_result;
static uint8_t g_cleanup_events[8];
static size_t g_cleanup_event_count;
static size_t g_uart_budget;
static uint8_t g_uart_idle;
static uint8_t g_queued_pending_observed;
static uint8_t g_uart_bytes[1024];
static size_t g_uart_length;
static test_schedule_t g_schedule;
static const uint8_t *g_external_tx;
static uint8_t *g_external_rx;
static uint32_t g_external_tx_size;
static uint32_t g_external_rx_size;
static uint32_t g_external_tx_done;
static uint32_t g_external_rx_done;
static uint32_t g_external_operation_generation;
static uint32_t g_external_operation_kind;
static uint8_t g_external_operation_active;
static uint32_t g_external_request_id;
static uint64_t g_external_requested_features;

static void require_true(uint8_t condition, const char *message)
{
    if(condition)
        return;
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
}

static void reset_run(void)
{
    micropython_capability_bridge_cleanup();
    g_now_us = 0U;
    g_sleep_calls = 0U;
    g_interrupt_poll = 0U;
    g_interrupt_on_poll = 0U;
    g_vm_hook_calls = 0U;
    g_unwind_before_ack = 0U;
    g_external_suspend_calls = 0U;
    g_external_resume_calls = 0U;
    g_illum_restore_calls = 0U;
    g_rgb_restore_calls = 0U;
    g_light_writes = 0U;
    g_uart_init_calls = 0U;
    g_overlay_acquire_calls = 0U;
    g_overlay_present_calls = 0U;
    g_overlay_release_calls = 0U;
    g_overlay_acquired_run_id = 0U;
    g_overlay_released_run_id = 0U;
    g_overlay_command_count = 0U;
    g_overlay_text_length = 0U;
    memset(g_overlay_commands, 0, sizeof(g_overlay_commands));
    memset(g_overlay_text, 0, sizeof(g_overlay_text));
    g_overlay_present_result = LCD_OVERLAY_PRESENT_OK;
    memset(g_cleanup_events, 0, sizeof(g_cleanup_events));
    g_cleanup_event_count = 0U;
    g_uart_budget = sizeof(g_uart_bytes);
    g_uart_idle = 1U;
    g_queued_pending_observed = 0U;
    g_uart_length = 0U;
    g_external_tx = NULL;
    g_external_rx = NULL;
    g_external_tx_size = 0U;
    g_external_rx_size = 0U;
    g_external_tx_done = 0U;
    g_external_rx_done = 0U;
    g_external_operation_generation = 0U;
    g_external_operation_kind = 0U;
    g_external_operation_active = 0U;
    g_external_request_id = 0U;
    g_external_requested_features = 0U;
    g_schedule = SCHEDULE_TICK_EACH_SLEEP;
    g_run_id++;
    micropython_capability_bridge_prepare(g_run_id);
}

static micropython_binding_result_t call_binding(
    micropython_binding_op_t operation, const uint32_t arguments[6],
    const uint8_t *input, size_t input_length)
{
    size_t output_length = 0U;

    return micropython_binding_call(operation, arguments,
                                    input, input_length,
                                    NULL, 0U, &output_length);
}

static micropython_binding_result_t call_binding_with_output(
    micropython_binding_op_t operation, const uint32_t arguments[6],
    const uint8_t *input, size_t input_length,
    uint8_t *output, size_t output_capacity, size_t *output_length)
{
    return micropython_binding_call(
        operation, arguments, input, input_length,
        output, output_capacity, output_length);
}

static void test_timeout_cancels_before_dispatch(void)
{
    uint32_t arguments[6] = {60U, 0U, 0U, 0U, 0U, 0U};
    micropython_binding_result_t result;

    reset_run();
    g_schedule = SCHEDULE_TIMEOUT_BEFORE_TICK;
    result = call_binding(MICROPYTHON_BINDING_OP_LED,
                          arguments, NULL, 0U);
    require_true(result == MICROPYTHON_BINDING_ERROR_TIMEOUT,
                 "RPC deadline must report timeout");
    require_true(g_light_writes == 0U,
                 "cancelled request must not dispatch a late light write");
    require_true(micropython_capability_bridge_test_cancel_acknowledged(),
                 "timeout must wait for a core-0 cancel acknowledgement");
    micropython_capability_bridge_tick();
    micropython_capability_bridge_tick();
    require_true(g_light_writes == 0U,
                 "acknowledged request must stay quiescent on later ticks");
}

static void test_stop_acknowledged_before_vm_unwind(void)
{
    static const uint8_t data[] = {'S', 'T', 'O', 'P'};
    uint32_t arguments[6] = {0U, 0U, 0U, 0U, 0U, 0U};
    micropython_binding_result_t result;
    size_t stopped_length;

    reset_run();
    g_uart_budget = 1U;
    g_interrupt_on_poll = 2U;
    result = call_binding(MICROPYTHON_BINDING_OP_UART_WRITE,
                          arguments, data, sizeof(data));
    require_true(result == MICROPYTHON_BINDING_ERROR_NOT_ACTIVE,
                 "host VM hook returns through the unreachable safety path");
    require_true(g_vm_hook_calls == 1U,
                 "pending stop must be delivered through the VM hook");
    require_true(g_unwind_before_ack == 0U,
                 "VM unwind must happen only after cancel acknowledgement");
    require_true(g_uart_length == 1U,
                 "stop fixture must interrupt an accepted UART request");
    stopped_length = g_uart_length;
    micropython_capability_bridge_tick();
    require_true(g_uart_length == stopped_length,
                 "stopped UART request must not progress after VM unwind");
}

static void test_uart_timeout_prevents_late_completion(void)
{
    static const uint8_t first[] = {'A', 'B', 'C', 'D'};
    static const uint8_t second[] = {'X', 'Y'};
    uint32_t arguments[6] = {0U, 0U, 0U, 0U, 0U, 0U};
    micropython_binding_result_t result;
    size_t stopped_length;

    reset_run();
    g_uart_budget = 1U;
    g_schedule = SCHEDULE_UART_TICK_THEN_TIMEOUT;
    result = call_binding(MICROPYTHON_BINDING_OP_UART_WRITE,
                          arguments, first, sizeof(first));
    require_true(result == MICROPYTHON_BINDING_ERROR_TIMEOUT,
                 "in-flight UART request must time out");
    require_true(g_uart_length == 1U && g_uart_bytes[0] == 'A',
                 "UART tick must make bounded partial progress");
    require_true(micropython_capability_bridge_test_cancel_acknowledged(),
                 "in-flight UART cancel must be acknowledged");
    stopped_length = g_uart_length;
    micropython_capability_bridge_tick();
    micropython_capability_bridge_tick();
    require_true(g_uart_length == stopped_length,
                 "UART must not write after cancellation completion");

    g_schedule = SCHEDULE_TICK_EACH_SLEEP;
    g_uart_budget = sizeof(g_uart_bytes);
    g_sleep_calls = 0U;
    result = call_binding(MICROPYTHON_BINDING_OP_UART_WRITE,
                          arguments, second, sizeof(second));
    require_true(result == MICROPYTHON_BINDING_OK,
                 "acknowledged slot must be reusable");
    require_true(g_uart_length == 3U &&
                 g_uart_bytes[1] == 'X' && g_uart_bytes[2] == 'Y',
                 "next request must own an intact fresh context");
}

static void test_cleanup_is_idempotent(void)
{
    uint32_t uart_arguments[6] = {115200U, 0U, 0U, 0U, 0U, 0U};
    uint32_t led_arguments[6] = {50U, 0U, 0U, 0U, 0U, 0U};

    reset_run();
    require_true(call_binding(MICROPYTHON_BINDING_OP_UART_INIT,
                              uart_arguments, NULL, 0U) ==
                     MICROPYTHON_BINDING_OK,
                 "UART init fixture must succeed");
    require_true(call_binding(MICROPYTHON_BINDING_OP_LED,
                              led_arguments, NULL, 0U) ==
                     MICROPYTHON_BINDING_OK,
                 "LED fixture must succeed");
    micropython_capability_bridge_cleanup();
    micropython_capability_bridge_cleanup();
    require_true(g_external_suspend_calls == 1U &&
                 g_external_resume_calls == 1U,
                 "external connector lease must be restored exactly once");
    require_true(g_illum_restore_calls == 1U && g_rgb_restore_calls == 0U,
                 "only the changed light setting must be restored once");
    require_true(g_cleanup_event_count == 3U &&
                 g_cleanup_events[0] == 1U &&
                 g_cleanup_events[1] == 2U &&
                 g_cleanup_events[2] == 4U,
                 "cleanup must follow external, lights, then display order");
}

static void test_uart_completion_waits_for_transmitter_drain(void)
{
    static const uint8_t data[] = {'D', 'R', 'N'};
    uint32_t arguments[6] = {0U, 0U, 0U, 0U, 0U, 0U};

    reset_run();
    g_uart_idle = 0U;
    g_schedule = SCHEDULE_UART_DRAIN;
    require_true(call_binding(MICROPYTHON_BINDING_OP_UART_WRITE,
                              arguments, data, sizeof(data)) ==
                     MICROPYTHON_BINDING_OK,
                 "drained UART request must complete");
    require_true(g_uart_length == sizeof(data),
                 "drain fixture must queue every byte");
    require_true(g_queued_pending_observed,
                 "queue completion must remain pending until TX idle");
}

static void test_i2c_controller_binding_uses_public_provider(void)
{
    static const uint8_t prefix[20] = {
        0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U,
        10U, 11U, 12U, 13U, 14U, 15U, 16U, 17U, 18U, 19U,
    };
    uint32_t arguments[6] = {0x50U, 40U, 0U, 0U, 0U, 0U};
    uint8_t output[40] = {0};
    size_t output_length = 0U;

    reset_run();
    require_true(call_binding_with_output(
                     MICROPYTHON_BINDING_OP_I2C_READ,
                     arguments, prefix, sizeof(prefix),
                     output, sizeof(output), &output_length) ==
                     MICROPYTHON_BINDING_OK,
                 "I2C combined write/read fixture must succeed");
    require_true(output_length == sizeof(output),
                 "I2C read must publish the complete provider RX prefix");
    for(uint32_t index = 0U; index < sizeof(output); ++index)
        require_true(output[index] == (uint8_t)(0x80U + index),
                     "I2C binding output must come from provider progress");
    require_true(g_external_request_id == HK_CAPABILITY_ID_EXTERNAL_LINK &&
                 g_external_requested_features ==
                     (HK_EXTERNAL_LINK_FEATURE_UART |
                      HK_EXTERNAL_LINK_FEATURE_I2C_CONTROLLER),
                 "MicroPython must acquire the public external-link provider ID");
    require_true(g_external_tx_done == sizeof(prefix) &&
                 g_external_rx_done == sizeof(output),
                 "one provider operation must own the whole I2C transaction");
}

static void test_display_stages_until_present(void)
{
    static const uint8_t text[] = {'H', 'i'};
    uint32_t clear[6] = {0x1234U, 0U, 0U, 0U, 0U, 0U};
    uint32_t label[6] = {7U, 9U, 0xFFFFU, 0x0000U, 0U, 0U};
    uint32_t rect[6] = {11U, 13U, 17U, 19U, 0x07E0U, 1U};
    uint32_t none[6] = {0U, 0U, 0U, 0U, 0U, 0U};

    reset_run();
    require_true(g_overlay_acquire_calls == 1U &&
                 g_overlay_acquired_run_id == g_run_id,
                 "run prepare must acquire one display lease");
    require_true(call_binding(MICROPYTHON_BINDING_OP_DISPLAY_CLEAR,
                              clear, NULL, 0U) == MICROPYTHON_BINDING_OK,
                 "display clear must stage");
    require_true(call_binding(MICROPYTHON_BINDING_OP_DISPLAY_TEXT,
                              label, text, sizeof(text)) ==
                     MICROPYTHON_BINDING_OK,
                 "display text must stage");
    require_true(call_binding(MICROPYTHON_BINDING_OP_DISPLAY_RECT,
                              rect, NULL, 0U) == MICROPYTHON_BINDING_OK,
                 "display rectangle must stage");
    require_true(g_overlay_present_calls == 0U,
                 "staging commands must not write the panel");
    require_true(call_binding(MICROPYTHON_BINDING_OP_DISPLAY_PRESENT,
                              none, NULL, 0U) == MICROPYTHON_BINDING_OK,
                 "explicit display present must succeed");
    require_true(g_overlay_present_calls == 1U &&
                 g_overlay_command_count == 4U &&
                 g_overlay_text_length == sizeof(text),
                 "present must publish the complete staged frame");
    require_true(g_overlay_commands[0].type == LCD_OVERLAY_COMMAND_CLEAR &&
                 g_overlay_commands[0].color == 0x1234U,
                 "presented clear command must preserve its color");
    require_true(g_overlay_commands[1].type == LCD_OVERLAY_COMMAND_FILL &&
                 g_overlay_commands[2].type == LCD_OVERLAY_COMMAND_TEXT &&
                 g_overlay_commands[2].x == 7U &&
                 g_overlay_commands[2].y == 9U &&
                 !memcmp(g_overlay_text, text, sizeof(text)),
                 "text background, glyph command, and bytes must be intact");
    require_true(g_overlay_commands[3].type == LCD_OVERLAY_COMMAND_FILL &&
                 g_overlay_commands[3].width == 17U &&
                 g_overlay_commands[3].height == 19U &&
                 g_overlay_commands[3].filled,
                 "presented rectangle must preserve geometry");
}

static void test_display_cancel_preserves_stage_for_retry(void)
{
    static const uint8_t text[] = {'R', 'E', 'T', 'R', 'Y'};
    uint32_t clear[6] = {0U, 0U, 0U, 0U, 0U, 0U};
    uint32_t label[6] = {1U, 2U, 0xFFFFU, 0U, 0U, 0U};
    uint32_t none[6] = {0U, 0U, 0U, 0U, 0U, 0U};

    reset_run();
    require_true(call_binding(MICROPYTHON_BINDING_OP_DISPLAY_CLEAR,
                              clear, NULL, 0U) == MICROPYTHON_BINDING_OK &&
                 call_binding(MICROPYTHON_BINDING_OP_DISPLAY_TEXT,
                              label, text, sizeof(text)) ==
                     MICROPYTHON_BINDING_OK,
                 "display retry fixture must stage a frame");
    g_overlay_present_result = LCD_OVERLAY_PRESENT_CANCELLED;
    require_true(call_binding(MICROPYTHON_BINDING_OP_DISPLAY_PRESENT,
                              none, NULL, 0U) ==
                     MICROPYTHON_BINDING_ERROR_TIMEOUT,
                 "cancelled display transfer must report timeout");
    require_true(g_overlay_present_calls == 1U &&
                 g_overlay_command_count == 3U &&
                 !memcmp(g_overlay_text, text, sizeof(text)),
                 "cancelled present must receive an intact frame");
    g_overlay_present_result = LCD_OVERLAY_PRESENT_OK;
    require_true(call_binding(MICROPYTHON_BINDING_OP_DISPLAY_PRESENT,
                              none, NULL, 0U) == MICROPYTHON_BINDING_OK,
                 "cancelled staged frame must be retryable");
    require_true(g_overlay_present_calls == 2U &&
                 g_overlay_command_count == 3U &&
                 !memcmp(g_overlay_text, text, sizeof(text)),
                 "retry must publish the same staged frame");
}

static void test_display_limit_clear_recovery_and_cleanup(void)
{
    static const uint8_t text[] = {'X'};
    uint32_t rect[6] = {1U, 1U, 2U, 2U, 0xFFFFU, 0U};
    uint32_t label[6] = {1U, 1U, 0xFFFFU, 0U, 0U, 0U};
    uint32_t clear[6] = {0x0020U, 0U, 0U, 0U, 0U, 0U};
    uint32_t none[6] = {0U, 0U, 0U, 0U, 0U, 0U};

    reset_run();
    for(size_t i = 0U; i < LCD_OVERLAY_COMMAND_MAX - 1U; i++)
        require_true(call_binding(MICROPYTHON_BINDING_OP_DISPLAY_RECT,
                                  rect, NULL, 0U) ==
                         MICROPYTHON_BINDING_OK,
                     "atomic text fixture must fill all but one command");
    require_true(call_binding(MICROPYTHON_BINDING_OP_DISPLAY_TEXT,
                              label, text, sizeof(text)) ==
                     MICROPYTHON_BINDING_ERROR_LIMIT,
                 "two-command text append must report capacity failure");
    require_true(call_binding(MICROPYTHON_BINDING_OP_DISPLAY_PRESENT,
                              none, NULL, 0U) == MICROPYTHON_BINDING_OK &&
                 g_overlay_command_count == LCD_OVERLAY_COMMAND_MAX - 1U &&
                 g_overlay_text_length == 0U,
                 "failed text append must roll back its background command");

    reset_run();
    for(size_t i = 0U; i < LCD_OVERLAY_COMMAND_MAX; i++)
        require_true(call_binding(MICROPYTHON_BINDING_OP_DISPLAY_RECT,
                                  rect, NULL, 0U) ==
                         MICROPYTHON_BINDING_OK,
                     "bounded display command buffer must accept its limit");
    require_true(call_binding(MICROPYTHON_BINDING_OP_DISPLAY_RECT,
                              rect, NULL, 0U) ==
                     MICROPYTHON_BINDING_ERROR_LIMIT,
                 "display command overflow must be explicit");
    require_true(call_binding(MICROPYTHON_BINDING_OP_DISPLAY_PRESENT,
                              none, NULL, 0U) == MICROPYTHON_BINDING_OK &&
                 g_overlay_present_calls == 1U &&
                 g_overlay_command_count == LCD_OVERLAY_COMMAND_MAX,
                 "failed append must leave the valid bounded frame presentable");
    require_true(call_binding(MICROPYTHON_BINDING_OP_DISPLAY_CLEAR,
                              clear, NULL, 0U) == MICROPYTHON_BINDING_OK &&
                 call_binding(MICROPYTHON_BINDING_OP_DISPLAY_PRESENT,
                              none, NULL, 0U) == MICROPYTHON_BINDING_OK,
                 "clear must recover from staging overflow");
    require_true(g_overlay_present_calls == 2U &&
                 g_overlay_command_count == 1U &&
                 g_overlay_commands[0].type == LCD_OVERLAY_COMMAND_CLEAR,
                 "recovered frame must contain only the new clear command");
    micropython_capability_bridge_cleanup();
    micropython_capability_bridge_cleanup();
    require_true(g_overlay_release_calls == 1U &&
                 g_overlay_released_run_id == g_run_id,
                 "display lease must be released exactly once for its run");
}

int main(void)
{
    test_timeout_cancels_before_dispatch();
    test_stop_acknowledged_before_vm_unwind();
    test_uart_timeout_prevents_late_completion();
    test_cleanup_is_idempotent();
    test_uart_completion_waits_for_transmitter_drain();
    test_i2c_controller_binding_uses_public_provider();
    test_display_stages_until_present();
    test_display_cancel_preserves_stage_for_retry();
    test_display_limit_clear_recovery_and_cleanup();
    micropython_capability_bridge_cleanup();
    puts("MICROPYTHON_BINDINGS_OK cases=9");
    return 0;
}

void external_link_service_suspend(void) { g_external_suspend_calls++; }
void external_link_service_resume(void)
{
    g_external_resume_calls++;
    g_cleanup_events[g_cleanup_event_count++] = 1U;
}
void settings_lights_suspend(uint32_t channels)
{
    (void)channels;
}
void settings_lights_restore(uint32_t channels)
{
    if(channels & HK_LIGHTS_CHANNEL_ILLUMINATION)
    {
        g_illum_restore_calls++;
        g_cleanup_events[g_cleanup_event_count++] = 2U;
    }
    if(channels & HK_LIGHTS_CHANNEL_RGB)
    {
        g_rgb_restore_calls++;
        g_cleanup_events[g_cleanup_event_count++] = 3U;
    }
}
hk_owner_t capability_client_consumer_owner(const char *consumer_id)
{
    hk_owner_t owner = {1U, 1U};

    if(consumer_id && strstr(consumer_id, "rgb"))
        owner.slot = 2U;
    return owner;
}
uint32_t hk_input_state(void) { return 0x0aU; }

hk_result_t hk_display_acquire(
    hk_owner_t owner, const hk_capability_request_t *request,
    uint32_t plane, hk_display_t *handle)
{
    (void)request;
    if(!handle || plane != HK_DISPLAY_PLANE_OVERLAY)
        return HK_ERR_INVALID_ARGUMENT;
    g_overlay_acquire_calls++;
    g_overlay_acquired_run_id = g_run_id;
    handle->lease = (hk_lease_t){
        7U, 1U, owner, HK_CAPABILITY_ID_DISPLAY,
    };
    return HK_OK;
}

hk_result_t hk_display_release(
    hk_owner_t owner, hk_deadline_t deadline, hk_display_t *handle)
{
    (void)owner;
    (void)deadline;
    g_overlay_release_calls++;
    g_overlay_released_run_id = g_run_id;
    g_cleanup_events[g_cleanup_event_count++] = 4U;
    if(handle)
        handle->lease = HK_LEASE_NONE;
    return HK_OK;
}

hk_result_t hk_display_begin_batch(
    hk_owner_t owner, const hk_display_t *handle)
{
    (void)owner;
    (void)handle;
    g_overlay_command_count = 0U;
    g_overlay_text_length = 0U;
    return HK_OK;
}

static hk_result_t display_append(
    uint8_t type, const hk_display_rect_t *rect, uint16_t color,
    const char *text, uint32_t text_bytes, uint8_t filled)
{
    test_display_command_t *command;

    if(g_overlay_command_count >= LCD_OVERLAY_COMMAND_MAX ||
       text_bytes > LCD_OVERLAY_TEXT_MAX - g_overlay_text_length)
        return HK_ERR_LIMIT;
    command = &g_overlay_commands[g_overlay_command_count++];
    *command = (test_display_command_t){
        type, filled, rect ? (uint16_t)rect->x : 0U,
        rect ? (uint16_t)rect->y : 0U,
        rect ? (uint16_t)rect->width : 320U,
        rect ? (uint16_t)rect->height : 240U,
        color, (uint16_t)g_overlay_text_length, (uint16_t)text_bytes,
    };
    if(text_bytes)
    {
        memcpy(g_overlay_text + g_overlay_text_length, text, text_bytes);
        g_overlay_text_length += text_bytes;
    }
    return HK_OK;
}

hk_result_t hk_display_clear(
    hk_owner_t owner, const hk_display_t *handle, uint16_t color)
{
    (void)owner;
    (void)handle;
    return display_append(
        LCD_OVERLAY_COMMAND_CLEAR, NULL, color, NULL, 0U, 1U);
}

hk_result_t hk_display_fill_rect(
    hk_owner_t owner, const hk_display_t *handle,
    const hk_display_rect_t *rect, uint16_t color)
{
    (void)owner;
    (void)handle;
    return display_append(
        LCD_OVERLAY_COMMAND_FILL, rect, color, NULL, 0U, 1U);
}

hk_result_t hk_display_stroke_rect(
    hk_owner_t owner, const hk_display_t *handle,
    const hk_display_rect_t *rect, uint16_t color)
{
    (void)owner;
    (void)handle;
    return display_append(
        LCD_OVERLAY_COMMAND_RECT, rect, color, NULL, 0U, 0U);
}

hk_result_t hk_display_text(
    hk_owner_t owner, const hk_display_t *handle,
    const hk_display_rect_t *bounds, const char *utf8,
    uint32_t size_bytes, uint16_t color)
{
    (void)owner;
    (void)handle;
    return display_append(
        LCD_OVERLAY_COMMAND_TEXT, bounds, color,
        utf8, size_bytes, 0U);
}

hk_result_t hk_display_present(
    hk_owner_t owner, const hk_display_t *handle,
    hk_deadline_t deadline, const hk_cancel_t *cancel)
{
    (void)owner;
    (void)handle;
    (void)deadline;
    if(cancel && cancel->probe && cancel->probe(cancel->context))
        return HK_ERR_CANCELLED;
    g_overlay_present_calls++;
    return g_overlay_present_result;
}

hk_result_t hk_display_stage_checkpoint(
    hk_owner_t owner, const hk_display_t *handle,
    uint16_t *commands, uint16_t *text_bytes)
{
    (void)owner;
    (void)handle;
    *commands = (uint16_t)g_overlay_command_count;
    *text_bytes = (uint16_t)g_overlay_text_length;
    return HK_OK;
}

hk_result_t hk_display_stage_restore(
    hk_owner_t owner, const hk_display_t *handle,
    uint16_t commands, uint16_t text_bytes)
{
    (void)owner;
    (void)handle;
    if(commands > LCD_OVERLAY_COMMAND_MAX ||
       text_bytes > LCD_OVERLAY_TEXT_MAX)
        return HK_ERR_INVALID_ARGUMENT;
    g_overlay_command_count = commands;
    g_overlay_text_length = text_bytes;
    return HK_OK;
}

hk_result_t hk_display_stage_keep_last_clear(
    hk_owner_t owner, const hk_display_t *handle)
{
    (void)owner;
    (void)handle;
    return HK_OK;
}

hk_result_t hk_lights_acquire(
    hk_owner_t owner, const hk_capability_request_t *request,
    uint32_t channels, hk_lights_t *handle)
{
    static uint32_t generation = 1U;

    (void)request;
    if(!handle || channels == 0U)
        return HK_ERR_INVALID_ARGUMENT;
    handle->lease = (hk_lease_t){
        channels, generation++, owner, HK_CAPABILITY_ID_LIGHTS,
    };
    return HK_OK;
}

hk_result_t hk_lights_release(
    hk_owner_t owner, hk_deadline_t deadline, hk_lights_t *handle)
{
    (void)owner;
    (void)deadline;
    if(!handle)
        return HK_ERR_INVALID_ARGUMENT;
    handle->lease = HK_LEASE_NONE;
    return HK_OK;
}

hk_result_t hk_lights_set_level(
    hk_owner_t owner, const hk_lights_t *handle, uint32_t channel,
    uint16_t level, hk_deadline_t deadline, const hk_cancel_t *cancel)
{
    (void)owner;
    (void)handle;
    (void)channel;
    (void)level;
    (void)deadline;
    if(cancel && cancel->probe && cancel->probe(cancel->context))
        return HK_ERR_CANCELLED;
    g_light_writes++;
    return HK_OK;
}

hk_result_t hk_lights_set_rgb(
    hk_owner_t owner, const hk_lights_t *handle, uint16_t red,
    uint16_t green, uint16_t blue, hk_deadline_t deadline,
    const hk_cancel_t *cancel)
{
    (void)owner;
    (void)handle;
    (void)red;
    (void)green;
    (void)blue;
    (void)deadline;
    if(cancel && cancel->probe && cancel->probe(cancel->context))
        return HK_ERR_CANCELLED;
    g_light_writes++;
    return HK_OK;
}

hk_result_t hk_external_link_acquire(
    hk_owner_t owner, const hk_capability_request_t *request,
    uint64_t mode_features, hk_external_link_t *handle)
{
    if(!request || !handle)
        return HK_ERR_INVALID_ARGUMENT;
    g_external_request_id = request->id;
    g_external_requested_features = mode_features;
    handle->lease = (hk_lease_t){
        9U, 1U, owner, HK_CAPABILITY_ID_EXTERNAL_LINK,
    };
    return HK_OK;
}

hk_result_t hk_external_link_release(
    hk_owner_t owner, hk_deadline_t deadline, hk_external_link_t *handle)
{
    (void)owner;
    (void)deadline;
    if(!handle)
        return HK_ERR_INVALID_ARGUMENT;
    g_external_operation_active = 0U;
    handle->lease = HK_LEASE_NONE;
    return HK_OK;
}

hk_result_t hk_external_link_configure_uart(
    hk_owner_t owner, const hk_external_link_t *handle,
    const hk_external_link_uart_config_t *config)
{
    (void)owner;
    if(!handle || !config)
        return HK_ERR_INVALID_ARGUMENT;
    g_uart_init_calls++;
    return HK_OK;
}

hk_result_t hk_external_link_configure_i2c_controller(
    hk_owner_t owner, const hk_external_link_t *handle,
    const hk_external_link_i2c_controller_config_t *config)
{
    (void)owner;
    return handle && config ? HK_OK : HK_ERR_INVALID_ARGUMENT;
}

hk_result_t hk_external_link_uart_write_begin(
    hk_owner_t owner, const hk_external_link_t *handle,
    const hk_buffer_view_t *tx, hk_deadline_t deadline,
    const hk_cancel_t *cancel, hk_external_link_op_t *operation)
{
    (void)owner;
    (void)deadline;
    (void)cancel;
    if(!handle || !tx || !operation)
        return HK_ERR_INVALID_ARGUMENT;
    g_external_tx = (const uint8_t *)tx->data;
    g_external_rx = NULL;
    g_external_tx_size = tx->size_bytes;
    g_external_rx_size = 0U;
    g_external_tx_done = 0U;
    g_external_rx_done = 0U;
    g_external_operation_kind = HK_EXTERNAL_LINK_OP_UART_WRITE;
    g_external_operation_active = 1U;
    operation->slot = 1U;
    operation->generation = ++g_external_operation_generation;
    return HK_PENDING;
}

hk_result_t hk_external_link_uart_read(
    hk_owner_t owner, const hk_external_link_t *handle,
    hk_buffer_view_t *rx, uint32_t *received_bytes)
{
    (void)owner;
    (void)handle;
    (void)rx;
    *received_bytes = 0U;
    return HK_OK;
}

hk_result_t hk_external_link_i2c_transfer_begin(
    hk_owner_t owner, const hk_external_link_t *handle,
    const hk_external_link_i2c_transfer_t *transfer,
    hk_deadline_t deadline, const hk_cancel_t *cancel,
    hk_external_link_op_t *operation)
{
    (void)owner;
    (void)handle;
    (void)deadline;
    (void)cancel;
    if(!transfer || !operation)
        return HK_ERR_INVALID_ARGUMENT;
    g_external_tx = (const uint8_t *)transfer->tx.data;
    g_external_rx = (uint8_t *)transfer->rx.data;
    g_external_tx_size = transfer->tx.size_bytes;
    g_external_rx_size = transfer->rx.size_bytes;
    g_external_tx_done = 0U;
    g_external_rx_done = 0U;
    g_external_operation_kind = HK_EXTERNAL_LINK_OP_I2C_TRANSFER;
    g_external_operation_active = 1U;
    operation->slot = 1U;
    operation->generation = ++g_external_operation_generation;
    return HK_PENDING;
}

static void external_progress(hk_external_link_op_progress_t *progress)
{
    uint32_t flags = g_external_operation_active ? 0U :
        HK_EXTERNAL_LINK_PROGRESS_TERMINAL;

    *progress = (hk_external_link_op_progress_t){
        sizeof(*progress), HK_EXTERNAL_LINK_OP_PROGRESS_VERSION,
        g_external_operation_kind, g_external_tx_done, g_external_rx_done,
        flags, g_external_operation_active ? HK_PENDING : HK_OK, 0U,
    };
}

hk_result_t hk_external_link_poll(
    hk_owner_t owner, const hk_external_link_t *handle,
    const hk_external_link_op_t *operation,
    hk_external_link_op_progress_t *progress)
{
    uint32_t budget = 32U;

    (void)owner;
    (void)handle;
    (void)operation;
    if(!g_external_operation_active)
    {
        external_progress(progress);
        return HK_OK;
    }
    if(g_external_operation_kind == HK_EXTERNAL_LINK_OP_UART_WRITE)
    {
        uint32_t count = g_external_tx_size - g_external_tx_done;

        if(count > budget)
            count = budget;
        if(count > g_uart_budget)
            count = (uint32_t)g_uart_budget;
        if(count > sizeof(g_uart_bytes) - g_uart_length)
            count = (uint32_t)(sizeof(g_uart_bytes) - g_uart_length);
        memcpy(g_uart_bytes + g_uart_length,
               g_external_tx + g_external_tx_done, count);
        g_uart_length += count;
        g_external_tx_done += count;
        if(g_external_tx_done == g_external_tx_size && g_uart_idle)
            g_external_operation_active = 0U;
    }
    else
    {
        uint32_t count = g_external_tx_size - g_external_tx_done;

        if(count > budget)
            count = budget;
        g_external_tx_done += count;
        budget -= count;
        count = g_external_rx_size - g_external_rx_done;
        if(count > budget)
            count = budget;
        for(uint32_t index = 0U; index < count; index++)
            g_external_rx[g_external_rx_done + index] =
                (uint8_t)(0x80U + g_external_rx_done + index);
        g_external_rx_done += count;
        if(g_external_tx_done == g_external_tx_size &&
           g_external_rx_done == g_external_rx_size)
            g_external_operation_active = 0U;
    }
    external_progress(progress);
    return g_external_operation_active ? HK_PENDING : HK_OK;
}

hk_result_t hk_external_link_cancel(
    hk_owner_t owner, const hk_external_link_t *handle,
    const hk_external_link_op_t *operation,
    hk_external_link_op_progress_t *progress)
{
    (void)owner;
    (void)handle;
    (void)operation;
    g_external_operation_active = 0U;
    external_progress(progress);
    progress->terminal_result = HK_ERR_CANCELLED;
    return HK_ERR_CANCELLED;
}

hk_result_t hk_time_acquire(
    hk_owner_t owner, const hk_capability_request_t *request,
    hk_time_t *handle)
{
    (void)request;
    handle->lease = (hk_lease_t){5U, 1U, owner, HK_CAPABILITY_ID_TIME};
    return HK_OK;
}

hk_result_t hk_time_now_us(
    hk_owner_t owner, const hk_time_t *handle, uint64_t *value)
{
    (void)owner;
    (void)handle;
    *value = g_now_us;
    return HK_OK;
}

hk_result_t hk_time_deadline_after_us(
    hk_owner_t owner, const hk_time_t *handle,
    uint64_t duration_us, hk_deadline_t *deadline)
{
    (void)owner;
    (void)handle;
    deadline->at_us = g_now_us + duration_us;
    return HK_OK;
}

static void test_sleep_ms(uint32_t duration_ms)
{
    g_sleep_calls++;
    if(g_schedule == SCHEDULE_TIMEOUT_BEFORE_TICK && g_sleep_calls == 1U)
    {
        g_now_us += 600000ULL;
        return;
    }
    if(g_schedule == SCHEDULE_UART_TICK_THEN_TIMEOUT && g_sleep_calls == 1U)
    {
        micropython_capability_bridge_tick();
        g_now_us += 4000000ULL;
        return;
    }
    if(g_schedule == SCHEDULE_UART_DRAIN)
    {
        if(g_sleep_calls == 1U)
        {
            micropython_capability_bridge_tick();
            if(g_uart_length != 0U &&
               micropython_capability_bridge_test_request_pending())
                g_queued_pending_observed = 1U;
        }
        else
        {
            g_uart_idle = 1U;
            micropython_capability_bridge_tick();
        }
        g_now_us += (uint64_t)duration_ms * 1000ULL;
        return;
    }
    micropython_capability_bridge_tick();
    g_now_us += (uint64_t)duration_ms * 1000ULL;
}

hk_result_t hk_time_sleep_until(
    hk_owner_t owner, const hk_time_t *handle,
    hk_deadline_t wake_target, hk_deadline_t operation_deadline,
    const hk_cancel_t *cancel)
{
    uint64_t remaining;

    (void)owner;
    (void)handle;
    (void)operation_deadline;
    if(cancel && cancel->probe && cancel->probe(cancel->context))
        return HK_ERR_CANCELLED;
    remaining = wake_target.at_us > g_now_us ?
        wake_target.at_us - g_now_us : 0U;
    test_sleep_ms((uint32_t)((remaining + 999U) / 1000U));
    return HK_OK;
}

uint8_t micropython_runtime_interrupt_pending(void)
{
    g_interrupt_poll++;
    return g_interrupt_on_poll != 0U &&
           g_interrupt_poll >= g_interrupt_on_poll;
}

void micropython_runtime_vm_hook(void)
{
    g_vm_hook_calls++;
    if(!micropython_capability_bridge_test_cancel_acknowledged())
        g_unwind_before_ack++;
}
