#include <stddef.h>
#include <stdint.h>

#include <hackylens/capability/time.h>

#include "py/runtime.h"

#include "../../config/input_config.h"
#include "../../core/hk_capability_client.h"
#include "../../adapters/micropython/micropython_capability_bridge.h"
#include "../../services/micropython_runtime.h"

static hk_time_t s_binding_time;
static hk_owner_t s_binding_time_owner;

static hk_result_t binding_time_prepare(hk_owner_t *owner)
{
    static const hk_capability_request_t request = HK_TIME_REQUEST_0_1_INIT;

    *owner = capability_client_consumer_owner(
        "consumer:micropython-adapter");
    if(hk_owner_is_zero(*owner))
        return HK_ERR_STALE_HANDLE;
    if(owner->slot != s_binding_time_owner.slot ||
       owner->generation != s_binding_time_owner.generation ||
       hk_lease_is_zero(&s_binding_time.lease))
    {
        s_binding_time.lease = HK_LEASE_NONE;
        s_binding_time_owner = *owner;
        return hk_time_acquire(*owner, &request, &s_binding_time);
    }
    return HK_OK;
}

static void binding_time_raise(hk_result_t result)
{
    if(result == HK_ERR_LIMIT || result == HK_ERR_INVALID_ARGUMENT)
        mp_raise_ValueError(MP_ERROR_TEXT("time limit invalid"));
    mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("time capability error"));
}

static uint8_t binding_time_cancel(const void *context)
{
    (void)context;
    return micropython_runtime_interrupt_pending();
}

static void binding_raise(micropython_binding_result_t result)
{
    if(result == MICROPYTHON_BINDING_ERROR_INVALID_ARGUMENT ||
       result == MICROPYTHON_BINDING_ERROR_LIMIT)
        mp_raise_ValueError(MP_ERROR_TEXT("invalid binding argument"));
    if(result == MICROPYTHON_BINDING_ERROR_TIMEOUT)
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("binding timeout"));
    if(result == MICROPYTHON_BINDING_ERROR_BUSY)
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("binding busy"));
    if(result == MICROPYTHON_BINDING_ERROR_IO)
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("binding I/O error"));
    mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("binding inactive"));
}

static size_t binding_call(micropython_binding_op_t operation,
                           const uint32_t arguments[6],
                           const uint8_t *input, size_t input_length,
                           uint8_t *output, size_t output_capacity)
{
    size_t output_length = 0U;
    micropython_binding_result_t result = micropython_binding_call(
        operation, arguments, input, input_length,
        output, output_capacity, &output_length);

    if(result != MICROPYTHON_BINDING_OK)
        binding_raise(result);
    return output_length;
}

static uint32_t binding_uint(mp_obj_t object, uint32_t maximum)
{
    mp_int_t value = mp_obj_get_int(object);

    if(value < 0 || (mp_uint_t)value > maximum)
        mp_raise_ValueError(MP_ERROR_TEXT("integer out of range"));
    return (uint32_t)value;
}

static mp_obj_t binding_buttons(void)
{
    uint8_t output[4];
    uint32_t arguments[6] = {0};
    uint32_t value;

    (void)binding_call(MICROPYTHON_BINDING_OP_BUTTONS, arguments,
                       NULL, 0U, output, sizeof(output));
    value = (uint32_t)output[0] |
            ((uint32_t)output[1] << 8) |
            ((uint32_t)output[2] << 16) |
            ((uint32_t)output[3] << 24);
    return mp_obj_new_int_from_uint(value);
}
static MP_DEFINE_CONST_FUN_OBJ_0(binding_buttons_obj, binding_buttons);

static mp_obj_t binding_button(mp_obj_t mask_object)
{
    uint32_t mask = binding_uint(mask_object, BUTTON_ALL);
    mp_obj_t state = binding_buttons();

    return mp_obj_new_bool((mp_obj_get_int(state) & mask) != 0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(binding_button_obj, binding_button);

static mp_obj_t binding_ticks_ms(void)
{
    hk_owner_t owner;
    uint64_t now = 0U;
    hk_result_t result = binding_time_prepare(&owner);

    if(result == HK_OK)
        result = hk_time_now_us(owner, &s_binding_time, &now);
    if(result != HK_OK)
        binding_time_raise(result);
    return mp_obj_new_int_from_uint((mp_uint_t)(now / 1000ULL));
}
static MP_DEFINE_CONST_FUN_OBJ_0(binding_ticks_ms_obj, binding_ticks_ms);

static mp_obj_t binding_sleep_ms(mp_obj_t duration_object)
{
    uint32_t duration = binding_uint(
        duration_object, MICROPYTHON_RUNTIME_MAX_LIMIT_MS);
    hk_owner_t owner;
    hk_deadline_t wake;
    const hk_cancel_t cancel = {binding_time_cancel, NULL};
    hk_result_t result = binding_time_prepare(&owner);

    if(result == HK_OK)
        result = hk_time_deadline_after_us(
            owner, &s_binding_time, (uint64_t)duration * 1000ULL,
            &wake);
    if(result == HK_OK)
        result = hk_time_sleep_until(
            owner, &s_binding_time, wake, wake, &cancel);
    if(result == HK_ERR_CANCELLED)
        micropython_runtime_vm_hook();
    if(result != HK_OK)
        binding_time_raise(result);
    micropython_runtime_vm_hook();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(binding_sleep_ms_obj, binding_sleep_ms);

static mp_obj_t binding_display_clear(size_t count, const mp_obj_t *args)
{
    uint32_t arguments[6] = {0};

    if(count)
        arguments[0] = binding_uint(args[0], 0xFFFFU);
    (void)binding_call(MICROPYTHON_BINDING_OP_DISPLAY_CLEAR, arguments,
                       NULL, 0U, NULL, 0U);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(
    binding_display_clear_obj, 0, 1, binding_display_clear);

static mp_obj_t binding_display_text(size_t count, const mp_obj_t *args)
{
    uint32_t arguments[6] = {0};
    size_t text_length;
    const char *text;

    arguments[0] = binding_uint(args[0], 319U);
    arguments[1] = binding_uint(args[1], 239U);
    arguments[2] = count >= 4U ? binding_uint(args[3], 0xFFFFU) : 0xFFFFU;
    arguments[3] = count >= 5U ? binding_uint(args[4], 0xFFFFU) : 0U;
    text = (const char *)mp_obj_str_get_data(args[2], &text_length);
    if(text_length > MICROPYTHON_BINDING_DATA_MAX)
        mp_raise_ValueError(MP_ERROR_TEXT("text too long"));
    (void)binding_call(MICROPYTHON_BINDING_OP_DISPLAY_TEXT, arguments,
                       (const uint8_t *)text, text_length, NULL, 0U);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(
    binding_display_text_obj, 3, 5, binding_display_text);

static mp_obj_t binding_display_rect(size_t count, const mp_obj_t *args)
{
    uint32_t arguments[6] = {0};

    arguments[0] = binding_uint(args[0], 319U);
    arguments[1] = binding_uint(args[1], 239U);
    arguments[2] = binding_uint(args[2], 320U);
    arguments[3] = binding_uint(args[3], 240U);
    arguments[4] = binding_uint(args[4], 0xFFFFU);
    arguments[5] = count >= 6U && mp_obj_is_true(args[5]);
    (void)binding_call(MICROPYTHON_BINDING_OP_DISPLAY_RECT, arguments,
                       NULL, 0U, NULL, 0U);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(
    binding_display_rect_obj, 5, 6, binding_display_rect);

static mp_obj_t binding_display_present(void)
{
    uint32_t arguments[6] = {0};

    (void)binding_call(MICROPYTHON_BINDING_OP_DISPLAY_PRESENT, arguments,
                       NULL, 0U, NULL, 0U);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(binding_display_present_obj,
                                 binding_display_present);

static mp_obj_t binding_led(mp_obj_t brightness_object)
{
    uint32_t arguments[6] = {0};

    arguments[0] = binding_uint(brightness_object, 100U);
    (void)binding_call(MICROPYTHON_BINDING_OP_LED, arguments,
                       NULL, 0U, NULL, 0U);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(binding_led_obj, binding_led);

static mp_obj_t binding_rgb(mp_obj_t red_object, mp_obj_t green_object,
                            mp_obj_t blue_object)
{
    uint32_t arguments[6] = {0};

    arguments[0] = binding_uint(red_object, 255U);
    arguments[1] = binding_uint(green_object, 255U);
    arguments[2] = binding_uint(blue_object, 255U);
    (void)binding_call(MICROPYTHON_BINDING_OP_RGB, arguments,
                       NULL, 0U, NULL, 0U);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(binding_rgb_obj, binding_rgb);

static mp_obj_t binding_uart_init(size_t count, const mp_obj_t *args)
{
    uint32_t arguments[6] = {0};

    arguments[0] = count ? binding_uint(args[0], 2000000U) : 115200U;
    if(arguments[0] < 1200U)
        mp_raise_ValueError(MP_ERROR_TEXT("invalid baud"));
    (void)binding_call(MICROPYTHON_BINDING_OP_UART_INIT, arguments,
                       NULL, 0U, NULL, 0U);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(
    binding_uart_init_obj, 0, 1, binding_uart_init);

static mp_obj_t binding_uart_write(mp_obj_t data_object)
{
    size_t length;
    const uint8_t *data = (const uint8_t *)mp_obj_str_get_data(
        data_object, &length);
    uint32_t arguments[6] = {0};
    size_t position = 0U;

    while(position < length)
    {
        size_t chunk = length - position;
        if(chunk > MICROPYTHON_BINDING_DATA_MAX)
            chunk = MICROPYTHON_BINDING_DATA_MAX;
        (void)binding_call(MICROPYTHON_BINDING_OP_UART_WRITE, arguments,
                           data + position, chunk, NULL, 0U);
        position += chunk;
    }
    return mp_obj_new_int_from_uint(length);
}
static MP_DEFINE_CONST_FUN_OBJ_1(binding_uart_write_obj, binding_uart_write);

static mp_obj_t binding_uart_read(size_t count, const mp_obj_t *args)
{
    uint8_t data[MICROPYTHON_BINDING_DATA_MAX];
    uint32_t arguments[6] = {0};
    size_t length;

    arguments[0] = count ? binding_uint(
        args[0], MICROPYTHON_BINDING_DATA_MAX) : 64U;
    length = binding_call(MICROPYTHON_BINDING_OP_UART_READ, arguments,
                          NULL, 0U, data, arguments[0]);
    return mp_obj_new_bytes(data, length);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(
    binding_uart_read_obj, 0, 1, binding_uart_read);

static mp_obj_t binding_i2c_write(mp_obj_t address_object,
                                  mp_obj_t data_object)
{
    uint32_t arguments[6] = {0};
    size_t length;
    const uint8_t *data = (const uint8_t *)mp_obj_str_get_data(
        data_object, &length);

    arguments[0] = binding_uint(address_object, 0x7FU);
    if(arguments[0] == 0U || length > MICROPYTHON_BINDING_DATA_MAX)
        mp_raise_ValueError(MP_ERROR_TEXT("invalid I2C write"));
    (void)binding_call(MICROPYTHON_BINDING_OP_I2C_WRITE, arguments,
                       data, length, NULL, 0U);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(binding_i2c_write_obj, binding_i2c_write);

static mp_obj_t binding_i2c_read(size_t count, const mp_obj_t *args)
{
    uint8_t data[MICROPYTHON_BINDING_DATA_MAX];
    uint32_t arguments[6] = {0};
    const uint8_t *prefix = NULL;
    size_t prefix_length = 0U;
    size_t length;

    arguments[0] = binding_uint(args[0], 0x7FU);
    arguments[1] = binding_uint(args[1], MICROPYTHON_BINDING_DATA_MAX);
    if(arguments[0] == 0U)
        mp_raise_ValueError(MP_ERROR_TEXT("invalid I2C address"));
    if(count >= 3U)
        prefix = (const uint8_t *)mp_obj_str_get_data(args[2], &prefix_length);
    if(prefix_length > MICROPYTHON_BINDING_DATA_MAX)
        mp_raise_ValueError(MP_ERROR_TEXT("I2C prefix too long"));
    length = binding_call(MICROPYTHON_BINDING_OP_I2C_READ, arguments,
                          prefix, prefix_length, data, arguments[1]);
    return mp_obj_new_bytes(data, length);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(
    binding_i2c_read_obj, 2, 3, binding_i2c_read);

static const mp_rom_map_elem_t hackylens_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_hackylens) },
    { MP_ROM_QSTR(MP_QSTR_BUTTON_LEFT), MP_ROM_INT(BUTTON_LEFT) },
    { MP_ROM_QSTR(MP_QSTR_BUTTON_OK), MP_ROM_INT(BUTTON_OK) },
    { MP_ROM_QSTR(MP_QSTR_BUTTON_RIGHT), MP_ROM_INT(BUTTON_RIGHT) },
    { MP_ROM_QSTR(MP_QSTR_BUTTON_BACK), MP_ROM_INT(BUTTON_BACK) },
    { MP_ROM_QSTR(MP_QSTR_buttons), MP_ROM_PTR(&binding_buttons_obj) },
    { MP_ROM_QSTR(MP_QSTR_button), MP_ROM_PTR(&binding_button_obj) },
    { MP_ROM_QSTR(MP_QSTR_ticks_ms), MP_ROM_PTR(&binding_ticks_ms_obj) },
    { MP_ROM_QSTR(MP_QSTR_sleep_ms), MP_ROM_PTR(&binding_sleep_ms_obj) },
    { MP_ROM_QSTR(MP_QSTR_display_clear), MP_ROM_PTR(&binding_display_clear_obj) },
    { MP_ROM_QSTR(MP_QSTR_display_text), MP_ROM_PTR(&binding_display_text_obj) },
    { MP_ROM_QSTR(MP_QSTR_display_rect), MP_ROM_PTR(&binding_display_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_display_present), MP_ROM_PTR(&binding_display_present_obj) },
    { MP_ROM_QSTR(MP_QSTR_led), MP_ROM_PTR(&binding_led_obj) },
    { MP_ROM_QSTR(MP_QSTR_rgb), MP_ROM_PTR(&binding_rgb_obj) },
    { MP_ROM_QSTR(MP_QSTR_uart_init), MP_ROM_PTR(&binding_uart_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_uart_write), MP_ROM_PTR(&binding_uart_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_uart_read), MP_ROM_PTR(&binding_uart_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_i2c_write), MP_ROM_PTR(&binding_i2c_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_i2c_read), MP_ROM_PTR(&binding_i2c_read_obj) },
};
static MP_DEFINE_CONST_DICT(hackylens_module_globals,
                            hackylens_module_globals_table);

const mp_obj_module_t hackylens_user_module = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *)&hackylens_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_hackylens, hackylens_user_module);
