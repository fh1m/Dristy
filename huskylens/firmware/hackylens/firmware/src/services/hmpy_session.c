#include "hmpy_session.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "hk_config.h"
#include "debug_console_service.h"
#include "hmpy_codec.h"
#include "micropython_program.h"
#include "micropython_runtime.h"
#include "hal_time.h"
#include "hal_watchdog.h"
#include "inventory.h"
#include "../storage/userfs.h"

#define HMPY_SESSION_LEASE_US 10000000ULL
#define HMPY_READ_DATA_MAX 512U
#define HMPY_EVENT_DATA_MAX 504U
#define HMPY_UPLOAD_DATA_MAX (HMPY_MAX_PAYLOAD_SIZE - 8U)
#define HMPY_FORMAT_TOKEN "ERASE USERFS"

#define HMPY_CAP_FILES (1UL << 0)
#define HMPY_CAP_ATOMIC_UPLOAD (1UL << 1)
#define HMPY_CAP_STARTUP (1UL << 2)
#define HMPY_CAP_RUN_STOP (1UL << 3)
#define HMPY_CAP_STDOUT (1UL << 4)
#define HMPY_CAP_STDERR (1UL << 5)
#define HMPY_CAP_FORMAT (1UL << 6)
#define HMPY_CAP_BINDINGS_V1 (1UL << 7)
#define HMPY_CAP_BOOT_FLAGS (1UL << 8)

#define HMPY_BOOT_FLAG_WDT1_RECOVERY (1U << 0)

typedef struct
{
    uint8_t active;
    uint8_t read_active;
    uint8_t last_state;
    uint8_t last_exit;
    uint32_t last_run_id;
    uint32_t output_cursor;
    uint32_t diagnostic_cursor;
    uint32_t upload_id;
    uint32_t upload_last_offset;
    uint32_t upload_last_length;
    uint32_t read_request_id;
    uint32_t read_offset;
    uint32_t read_end;
    uint32_t read_size;
    uint64_t last_receive_us;
    char upload_name[USERFS_NAME_MAX + 1U];
    char read_name[USERFS_NAME_MAX + 1U];
    uint8_t upload_last_data[HMPY_UPLOAD_DATA_MAX];
    hmpy_stream_decoder_t decoder;
} hmpy_session_state_t;

typedef struct
{
    uint32_t request_id;
    uint8_t sent;
} hmpy_list_context_t;

static hmpy_session_state_t g_session;
static uint8_t g_payload[HMPY_MAX_PAYLOAD_SIZE];
static uint8_t g_wire[HMPY_MAX_WIRE_FRAME];

static uint32_t hmpy_rd32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static void hmpy_wr16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void hmpy_wr32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static void hmpy_wr64(uint8_t *data, uint64_t value)
{
    hmpy_wr32(data, (uint32_t)value);
    hmpy_wr32(data + 4U, (uint32_t)(value >> 32));
}

static uint8_t hmpy_send(uint8_t type, uint16_t flags,
                         uint32_t request_id, const uint8_t *payload,
                         uint32_t payload_length)
{
    size_t wire_length = 0U;
    hmpy_codec_status_t result = hmpy_frame_encode(
        type, flags, request_id, payload, payload_length,
        g_wire, sizeof(g_wire), &wire_length);

    if(result != HMPY_CODEC_OK)
        return 0U;
    debug_console_write_wire(g_wire, wire_length);
    return 1U;
}

static uint8_t hmpy_response(uint8_t type, uint32_t request_id,
                             const uint8_t *payload, uint32_t payload_length,
                             uint8_t more)
{
    uint16_t flags = HMPY_FLAG_RESPONSE;

    if(more)
        flags |= HMPY_FLAG_MORE;
    return hmpy_send(type, flags, request_id, payload, payload_length);
}

static void hmpy_error(uint8_t type, uint32_t request_id,
                       hmpy_error_code_t code, const char *detail)
{
    size_t detail_length = detail ? strlen(detail) : 0U;

    if(detail_length > HMPY_MAX_PAYLOAD_SIZE - 4U)
        detail_length = HMPY_MAX_PAYLOAD_SIZE - 4U;
    hmpy_wr16(g_payload, (uint16_t)code);
    hmpy_wr16(g_payload + 2U, (uint16_t)detail_length);
    if(detail_length)
        memcpy(g_payload + 4U, detail, detail_length);
    (void)hmpy_send(type, HMPY_FLAG_RESPONSE | HMPY_FLAG_ERROR,
                    request_id, g_payload, (uint32_t)detail_length + 4U);
}

static hmpy_error_code_t hmpy_userfs_error(userfs_result_t result)
{
    switch(result)
    {
    case USERFS_ERROR_INVALID_ARGUMENT: return HMPY_ERROR_INVALID_PAYLOAD;
    case USERFS_ERROR_NOT_FOUND: return HMPY_ERROR_NOT_FOUND;
    case USERFS_ERROR_EXISTS: return HMPY_ERROR_ALREADY_EXISTS;
    case USERFS_ERROR_NO_SPACE: return HMPY_ERROR_NO_SPACE;
    case USERFS_ERROR_BUSY: return HMPY_ERROR_BUSY;
    case USERFS_ERROR_OUT_OF_ORDER: return HMPY_ERROR_OFFSET_MISMATCH;
    case USERFS_ERROR_SIZE: return HMPY_ERROR_LIMIT_EXCEEDED;
    case USERFS_ERROR_CRC: return HMPY_ERROR_CRC_MISMATCH;
    case USERFS_ERROR_UNFORMATTED: return HMPY_ERROR_CONFIRMATION_REQUIRED;
    case USERFS_ERROR_UNSUPPORTED_FLASH: return HMPY_ERROR_PERMISSION_DENIED;
    case USERFS_ERROR_NOT_MOUNTED:
    case USERFS_ERROR_CORRUPT:
    case USERFS_ERROR_IO:
    default:
        return HMPY_ERROR_IO;
    }
}

static hmpy_error_code_t hmpy_program_error(
    micropython_program_result_t result)
{
    switch(result)
    {
    case MICROPYTHON_PROGRAM_INVALID_ARGUMENT:
        return HMPY_ERROR_INVALID_PAYLOAD;
    case MICROPYTHON_PROGRAM_NOT_FOUND:
        return HMPY_ERROR_NOT_FOUND;
    case MICROPYTHON_PROGRAM_TOO_LARGE:
        return HMPY_ERROR_LIMIT_EXCEEDED;
    case MICROPYTHON_PROGRAM_BUSY:
        return HMPY_ERROR_BUSY;
    case MICROPYTHON_PROGRAM_FILESYSTEM:
    case MICROPYTHON_PROGRAM_IO:
    default:
        return HMPY_ERROR_IO;
    }
}

static uint8_t hmpy_parse_name(const uint8_t *payload, uint32_t length,
                               uint32_t *position, char *name,
                               uint8_t allow_empty)
{
    uint32_t cursor;
    uint8_t name_length;

    if(!payload || !position || !name)
        return 0U;
    cursor = *position;
    if(cursor >= length)
        return 0U;
    name_length = payload[cursor++];
    if((!allow_empty && name_length == 0U) ||
       name_length > USERFS_NAME_MAX ||
       name_length > length - cursor)
        return 0U;
    if(name_length)
        memcpy(name, payload + cursor, name_length);
    name[name_length] = '\0';
    *position = cursor + name_length;
    return 1U;
}

static void hmpy_emit_file_changed(uint8_t operation, const char *name)
{
    size_t name_length = name ? strlen(name) : 0U;

    if(name_length > USERFS_NAME_MAX)
        name_length = USERFS_NAME_MAX;
    g_payload[0] = operation;
    g_payload[1] = (uint8_t)name_length;
    if(name_length)
        memcpy(g_payload + 2U, name, name_length);
    (void)hmpy_send(HMPY_MSG_FILE_CHANGED, 0U, 0U, g_payload,
                    (uint32_t)name_length + 2U);
}

static void hmpy_emit_dropped(uint8_t stream, uint32_t run_id,
                              uint32_t count)
{
    g_payload[0] = stream;
    hmpy_wr32(g_payload + 1U, run_id);
    hmpy_wr32(g_payload + 5U, count);
    (void)hmpy_send(HMPY_MSG_DROPPED, 0U, 0U, g_payload, 9U);
}

static void hmpy_handle_hello(const hmpy_frame_t *frame)
{
    static const char board[] = HK_BOARD_ID;
    userfs_status_t filesystem;
    micropython_runtime_status_t runtime;
    size_t version_length = strlen(HACKYLENS_VERSION);
    size_t board_length = sizeof(board) - 1U;
    uint32_t capabilities = HMPY_CAP_FILES | HMPY_CAP_ATOMIC_UPLOAD |
        HMPY_CAP_STARTUP | HMPY_CAP_RUN_STOP | HMPY_CAP_STDOUT |
        HMPY_CAP_STDERR | HMPY_CAP_FORMAT | HMPY_CAP_BINDINGS_V1 |
        HMPY_CAP_BOOT_FLAGS;
    uint32_t cursor = 0U;

    if(frame->payload_length != 0U)
    {
        hmpy_error(frame->type, frame->request_id,
                   HMPY_ERROR_INVALID_PAYLOAD, "hello payload");
        return;
    }
    (void)userfs_mount();
    userfs_get_status(&filesystem);
    micropython_runtime_get_status(&runtime);

    g_payload[cursor++] = HMPY_PROTOCOL_VERSION;
    g_payload[cursor++] = (uint8_t)filesystem.state;
    g_payload[cursor++] = (uint8_t)runtime.state;
    g_payload[cursor++] = hal_watchdog_reset_detected() ?
        HMPY_BOOT_FLAG_WDT1_RECOVERY : 0U;
    hmpy_wr32(g_payload + cursor, capabilities); cursor += 4U;
    hmpy_wr16(g_payload + cursor, HMPY_MAX_PAYLOAD_SIZE); cursor += 2U;
    hmpy_wr16(g_payload + cursor, HMPY_UPLOAD_DATA_MAX); cursor += 2U;
    hmpy_wr16(g_payload + cursor, USERFS_NAME_MAX); cursor += 2U;
    hmpy_wr16(g_payload + cursor, MICROPYTHON_RUNTIME_SOURCE_MAX); cursor += 2U;
    hmpy_wr32(g_payload + cursor, USERFS_FILE_MAX); cursor += 4U;
    hmpy_wr32(g_payload + cursor, filesystem.total_bytes); cursor += 4U;
    hmpy_wr32(g_payload + cursor, filesystem.used_bytes); cursor += 4U;
    g_payload[cursor++] = (uint8_t)version_length;
    memcpy(g_payload + cursor, HACKYLENS_VERSION, version_length);
    cursor += (uint32_t)version_length;
    g_payload[cursor++] = (uint8_t)board_length;
    memcpy(g_payload + cursor, board, board_length);
    cursor += (uint32_t)board_length;
    (void)hmpy_response(frame->type, frame->request_id,
                        g_payload, cursor, 0U);
}

static uint8_t hmpy_list_item(const char *name, uint32_t size, void *context)
{
    hmpy_list_context_t *list = (hmpy_list_context_t *)context;
    size_t name_length = strlen(name);

    g_payload[0] = (uint8_t)name_length;
    memcpy(g_payload + 1U, name, name_length);
    hmpy_wr32(g_payload + 1U + name_length, size);
    list->sent++;
    return hmpy_response(HMPY_MSG_LIST, list->request_id, g_payload,
                         (uint32_t)name_length + 5U, 1U);
}

static void hmpy_handle_list(const hmpy_frame_t *frame)
{
    hmpy_list_context_t context = {frame->request_id, 0U};
    userfs_result_t result;

    if(frame->payload_length != 0U)
    {
        hmpy_error(frame->type, frame->request_id,
                   HMPY_ERROR_INVALID_PAYLOAD, "list payload");
        return;
    }
    result = userfs_list(hmpy_list_item, &context);
    if(result != USERFS_OK)
    {
        hmpy_error(frame->type, frame->request_id,
                   hmpy_userfs_error(result), userfs_result_name(result));
        return;
    }
    /* A final empty response terminates the stream, including an empty list. */
    (void)hmpy_response(frame->type, frame->request_id, NULL, 0U, 0U);
}

static void hmpy_handle_stat(const hmpy_frame_t *frame)
{
    char name[USERFS_NAME_MAX + 1U];
    char startup[USERFS_NAME_MAX + 1U] = {0};
    uint32_t position = 0U;
    uint32_t size;
    userfs_result_t result;

    if(!hmpy_parse_name(frame->payload, frame->payload_length,
                        &position, name, 0U) ||
       position != frame->payload_length)
    {
        hmpy_error(frame->type, frame->request_id,
                   HMPY_ERROR_INVALID_PAYLOAD, "stat name");
        return;
    }
    result = userfs_stat(name, &size);
    if(result != USERFS_OK)
    {
        hmpy_error(frame->type, frame->request_id,
                   hmpy_userfs_error(result), userfs_result_name(result));
        return;
    }
    (void)userfs_get_startup(startup, sizeof(startup));
    hmpy_wr32(g_payload, size);
    g_payload[4] = strcmp(name, startup) == 0;
    (void)hmpy_response(frame->type, frame->request_id, g_payload, 5U, 0U);
}

static void hmpy_handle_read(const hmpy_frame_t *frame)
{
    char name[USERFS_NAME_MAX + 1U];
    uint32_t position = 0U;
    uint32_t offset;
    uint32_t requested;
    uint32_t size;
    userfs_result_t result;

    if(g_session.read_active)
    {
        hmpy_error(frame->type, frame->request_id, HMPY_ERROR_BUSY,
                   "read in progress");
        return;
    }
    if(!hmpy_parse_name(frame->payload, frame->payload_length,
                        &position, name, 0U) ||
       frame->payload_length - position != 8U)
    {
        hmpy_error(frame->type, frame->request_id,
                   HMPY_ERROR_INVALID_PAYLOAD, "read payload");
        return;
    }
    offset = hmpy_rd32(frame->payload + position);
    requested = hmpy_rd32(frame->payload + position + 4U);
    result = userfs_stat(name, &size);
    if(result != USERFS_OK)
    {
        hmpy_error(frame->type, frame->request_id,
                   hmpy_userfs_error(result), userfs_result_name(result));
        return;
    }
    if(offset > size)
    {
        hmpy_error(frame->type, frame->request_id,
                   HMPY_ERROR_OFFSET_MISMATCH, "offset past eof");
        return;
    }
    strncpy(g_session.read_name, name, USERFS_NAME_MAX);
    g_session.read_name[USERFS_NAME_MAX] = '\0';
    g_session.read_request_id = frame->request_id;
    g_session.read_offset = offset;
    g_session.read_size = size;
    if(requested == 0U || requested > size - offset)
        requested = size - offset;
    g_session.read_end = offset + requested;
    g_session.read_active = 1U;
}

static void hmpy_pump_read(void)
{
    size_t read_size = 0U;
    uint32_t remaining;
    size_t chunk;
    userfs_result_t result;

    if(!g_session.read_active)
        return;
    remaining = g_session.read_end - g_session.read_offset;
    chunk = remaining > HMPY_READ_DATA_MAX ? HMPY_READ_DATA_MAX : remaining;
    hmpy_wr32(g_payload, g_session.read_offset);
    hmpy_wr32(g_payload + 4U, g_session.read_size);
    result = userfs_read(g_session.read_name, g_session.read_offset,
                         g_payload + 8U, chunk, &read_size);
    if(result != USERFS_OK || read_size != chunk)
    {
        hmpy_error(HMPY_MSG_READ, g_session.read_request_id,
                   result == USERFS_OK ? HMPY_ERROR_IO : hmpy_userfs_error(result),
                   "read failed");
        g_session.read_active = 0U;
        return;
    }
    g_session.read_offset += (uint32_t)read_size;
    (void)hmpy_response(HMPY_MSG_READ, g_session.read_request_id,
                        g_payload, (uint32_t)read_size + 8U,
                        g_session.read_offset < g_session.read_end);
    /* A full 256 KiB read takes longer than the idle lease at 115200 baud.
     * Outbound chunks are progress for the outstanding host request. */
    g_session.last_receive_us = hal_time_us();
    if(g_session.read_offset >= g_session.read_end)
        g_session.read_active = 0U;
}

static void hmpy_handle_upload_begin(const hmpy_frame_t *frame)
{
    char name[USERFS_NAME_MAX + 1U];
    uint32_t position = 0U;
    uint32_t size;
    uint32_t crc;
    uint32_t upload_id;
    userfs_result_t result;

    if(!hmpy_parse_name(frame->payload, frame->payload_length,
                        &position, name, 0U) ||
       frame->payload_length - position != 8U)
    {
        hmpy_error(frame->type, frame->request_id,
                   HMPY_ERROR_INVALID_PAYLOAD, "upload begin");
        return;
    }
    size = hmpy_rd32(frame->payload + position);
    crc = hmpy_rd32(frame->payload + position + 4U);
    result = userfs_upload_begin(name, size, crc, &upload_id);
    if(result != USERFS_OK)
    {
        hmpy_error(frame->type, frame->request_id,
                   hmpy_userfs_error(result), userfs_result_name(result));
        return;
    }
    g_session.upload_id = upload_id;
    g_session.upload_last_length = 0U;
    strncpy(g_session.upload_name, name, USERFS_NAME_MAX);
    g_session.upload_name[USERFS_NAME_MAX] = '\0';
    hmpy_wr32(g_payload, upload_id);
    hmpy_wr32(g_payload + 4U, 0U);
    (void)hmpy_response(frame->type, frame->request_id, g_payload, 8U, 0U);
}

static void hmpy_handle_upload_chunk(const hmpy_frame_t *frame)
{
    uint32_t upload_id;
    uint32_t offset;
    uint32_t length;
    userfs_result_t result;

    if(frame->payload_length < 8U)
    {
        hmpy_error(frame->type, frame->request_id,
                   HMPY_ERROR_INVALID_PAYLOAD, "upload chunk");
        return;
    }
    upload_id = hmpy_rd32(frame->payload);
    offset = hmpy_rd32(frame->payload + 4U);
    length = frame->payload_length - 8U;
    if(upload_id != g_session.upload_id || !upload_id)
    {
        hmpy_error(frame->type, frame->request_id,
                   HMPY_ERROR_INVALID_REQUEST, "upload id");
        return;
    }
    if(length > HMPY_UPLOAD_DATA_MAX)
    {
        hmpy_error(frame->type, frame->request_id,
                   HMPY_ERROR_LIMIT_EXCEEDED, "chunk too large");
        return;
    }
    if(g_session.upload_last_length == length &&
       g_session.upload_last_offset == offset &&
       memcmp(g_session.upload_last_data, frame->payload + 8U, length) == 0)
    {
        hmpy_wr32(g_payload, upload_id);
        hmpy_wr32(g_payload + 4U, offset + length);
        (void)hmpy_response(frame->type, frame->request_id,
                            g_payload, 8U, 0U);
        return;
    }
    result = userfs_upload_write(upload_id, offset, frame->payload + 8U, length);
    if(result != USERFS_OK)
    {
        hmpy_error(frame->type, frame->request_id,
                   hmpy_userfs_error(result), userfs_result_name(result));
        return;
    }
    g_session.upload_last_offset = offset;
    g_session.upload_last_length = length;
    if(length)
        memcpy(g_session.upload_last_data, frame->payload + 8U, length);
    hmpy_wr32(g_payload, upload_id);
    hmpy_wr32(g_payload + 4U, offset + length);
    (void)hmpy_response(frame->type, frame->request_id, g_payload, 8U, 0U);
}

static void hmpy_handle_upload_finish(const hmpy_frame_t *frame, uint8_t abort)
{
    uint32_t upload_id;
    userfs_result_t result;

    if(frame->payload_length != 4U)
    {
        hmpy_error(frame->type, frame->request_id,
                   HMPY_ERROR_INVALID_PAYLOAD, "upload id");
        return;
    }
    upload_id = hmpy_rd32(frame->payload);
    if(!upload_id || upload_id != g_session.upload_id)
    {
        hmpy_error(frame->type, frame->request_id,
                   HMPY_ERROR_INVALID_REQUEST, "upload id");
        return;
    }
    result = abort ? userfs_upload_abort(upload_id) :
                     userfs_upload_commit(upload_id);
    if(result != USERFS_OK)
    {
        hmpy_error(frame->type, frame->request_id,
                   hmpy_userfs_error(result), userfs_result_name(result));
        return;
    }
    (void)hmpy_response(frame->type, frame->request_id, NULL, 0U, 0U);
    if(!abort)
        hmpy_emit_file_changed(1U, g_session.upload_name);
    g_session.upload_id = 0U;
    g_session.upload_last_length = 0U;
    g_session.upload_name[0] = '\0';
}

static void hmpy_handle_delete(const hmpy_frame_t *frame)
{
    char name[USERFS_NAME_MAX + 1U];
    uint32_t position = 0U;
    userfs_result_t result;

    if(!hmpy_parse_name(frame->payload, frame->payload_length,
                        &position, name, 0U) ||
       position != frame->payload_length)
    {
        hmpy_error(frame->type, frame->request_id,
                   HMPY_ERROR_INVALID_PAYLOAD, "delete name");
        return;
    }
    result = userfs_remove(name);
    if(result != USERFS_OK)
    {
        hmpy_error(frame->type, frame->request_id,
                   hmpy_userfs_error(result), userfs_result_name(result));
        return;
    }
    (void)hmpy_response(frame->type, frame->request_id, NULL, 0U, 0U);
    hmpy_emit_file_changed(2U, name);
}

static void hmpy_handle_set_startup(const hmpy_frame_t *frame)
{
    char name[USERFS_NAME_MAX + 1U];
    uint32_t position = 0U;
    userfs_result_t result;

    if(!hmpy_parse_name(frame->payload, frame->payload_length,
                        &position, name, 1U) ||
       position != frame->payload_length)
    {
        hmpy_error(frame->type, frame->request_id,
                   HMPY_ERROR_INVALID_PAYLOAD, "startup name");
        return;
    }
    result = userfs_set_startup(name);
    if(result != USERFS_OK)
    {
        hmpy_error(frame->type, frame->request_id,
                   hmpy_userfs_error(result), userfs_result_name(result));
        return;
    }
    (void)hmpy_response(frame->type, frame->request_id, NULL, 0U, 0U);
    hmpy_emit_file_changed(3U, name);
}

static void hmpy_handle_format(const hmpy_frame_t *frame)
{
    userfs_result_t result;
    size_t token_length = sizeof(HMPY_FORMAT_TOKEN) - 1U;

    if(g_session.upload_id)
    {
        hmpy_error(frame->type, frame->request_id,
                   HMPY_ERROR_BUSY, "upload active");
        return;
    }
    if(frame->payload_length != token_length ||
       memcmp(frame->payload, HMPY_FORMAT_TOKEN, token_length) != 0)
    {
        hmpy_error(frame->type, frame->request_id,
                   HMPY_ERROR_CONFIRMATION_REQUIRED, HMPY_FORMAT_TOKEN);
        return;
    }
    result = userfs_format_explicit();
    if(result != USERFS_OK)
    {
        hmpy_error(frame->type, frame->request_id,
                   hmpy_userfs_error(result), userfs_result_name(result));
        return;
    }
    (void)hmpy_response(frame->type, frame->request_id, NULL, 0U, 0U);
    hmpy_emit_file_changed(4U, "");
}

static void hmpy_handle_run(const hmpy_frame_t *frame)
{
    char name[USERFS_NAME_MAX + 1U];
    uint32_t position = 0U;
    uint32_t time_limit_ms;
    uint32_t run_id = 0U;
    micropython_program_result_t result;

    if(!hmpy_parse_name(frame->payload, frame->payload_length,
                        &position, name, 1U) ||
       frame->payload_length - position != 4U)
    {
        hmpy_error(frame->type, frame->request_id,
                   HMPY_ERROR_INVALID_PAYLOAD, "run payload");
        return;
    }
    time_limit_ms = hmpy_rd32(frame->payload + position);
    if(time_limit_ms > MICROPYTHON_RUNTIME_MAX_LIMIT_MS)
    {
        hmpy_error(frame->type, frame->request_id,
                   HMPY_ERROR_LIMIT_EXCEEDED, "run time limit");
        return;
    }
    result = name[0] ? micropython_program_run_file(name, time_limit_ms, &run_id) :
                       micropython_program_run_startup(time_limit_ms, &run_id);
    if(result != MICROPYTHON_PROGRAM_OK)
    {
        hmpy_error(frame->type, frame->request_id,
                   hmpy_program_error(result),
                   micropython_program_result_name(result));
        return;
    }
    hmpy_wr32(g_payload, run_id);
    (void)hmpy_response(frame->type, frame->request_id, g_payload, 4U, 0U);
}

static void hmpy_handle_stop(const hmpy_frame_t *frame)
{
    if(frame->payload_length != 0U)
    {
        hmpy_error(frame->type, frame->request_id,
                   HMPY_ERROR_INVALID_PAYLOAD, "stop payload");
        return;
    }
    if(!micropython_runtime_request_stop())
    {
        hmpy_error(frame->type, frame->request_id,
                   HMPY_ERROR_NOT_RUNNING, "not running");
        return;
    }
    (void)hmpy_response(frame->type, frame->request_id, NULL, 0U, 0U);
}

static uint32_t hmpy_status_payload(void)
{
    micropython_runtime_status_t runtime;
    userfs_status_t filesystem;

    micropython_runtime_get_status(&runtime);
    userfs_get_status(&filesystem);
    g_payload[0] = (uint8_t)runtime.state;
    g_payload[1] = (uint8_t)runtime.exit_reason;
    g_payload[2] = (uint8_t)filesystem.state;
    g_payload[3] = (uint8_t)filesystem.last_error;
    hmpy_wr32(g_payload + 4U, runtime.run_id);
    hmpy_wr32(g_payload + 8U, runtime.source_bytes);
    hmpy_wr32(g_payload + 12U, runtime.output_pending);
    hmpy_wr32(g_payload + 16U, runtime.output_dropped);
    hmpy_wr64(g_payload + 20U, runtime.started_us);
    hmpy_wr64(g_payload + 28U, runtime.heartbeat_us);
    hmpy_wr64(g_payload + 36U, runtime.deadline_us);
    hmpy_wr32(g_payload + 44U, filesystem.total_bytes);
    hmpy_wr32(g_payload + 48U, filesystem.used_bytes);
    return 52U;
}

static void hmpy_handle_status(const hmpy_frame_t *frame)
{
    if(frame->payload_length != 0U)
    {
        hmpy_error(frame->type, frame->request_id,
                   HMPY_ERROR_INVALID_PAYLOAD, "status payload");
        return;
    }
    (void)hmpy_response(frame->type, frame->request_id,
                        g_payload, hmpy_status_payload(), 0U);
}

static void hmpy_session_finish(void)
{
    if(g_session.upload_id)
        (void)userfs_upload_abort(g_session.upload_id);
    g_session.upload_id = 0U;
    g_session.read_active = 0U;
    g_session.active = 0U;
    hmpy_stream_decoder_reset(&g_session.decoder);
    debug_console_set_framed_mode(0U);
}

static void hmpy_handle_frame(const hmpy_frame_t *frame)
{
    if(frame->flags != 0U || frame->request_id == 0U ||
       (frame->type >= HMPY_MSG_STDOUT && frame->type <= HMPY_MSG_DROPPED))
    {
        if(frame->request_id)
            hmpy_error(frame->type, frame->request_id,
                       HMPY_ERROR_INVALID_REQUEST, "host request envelope");
        return;
    }

    if(g_session.read_active && frame->type != HMPY_MSG_PING &&
       frame->type != HMPY_MSG_STOP && frame->type != HMPY_MSG_STATUS &&
       frame->type != HMPY_MSG_SESSION_CLOSE)
    {
        hmpy_error(frame->type, frame->request_id, HMPY_ERROR_BUSY,
                   "read in progress");
        return;
    }

    switch(frame->type)
    {
    case HMPY_MSG_HELLO: hmpy_handle_hello(frame); break;
    case HMPY_MSG_LIST: hmpy_handle_list(frame); break;
    case HMPY_MSG_STAT: hmpy_handle_stat(frame); break;
    case HMPY_MSG_READ: hmpy_handle_read(frame); break;
    case HMPY_MSG_UPLOAD_BEGIN: hmpy_handle_upload_begin(frame); break;
    case HMPY_MSG_UPLOAD_CHUNK: hmpy_handle_upload_chunk(frame); break;
    case HMPY_MSG_UPLOAD_COMMIT: hmpy_handle_upload_finish(frame, 0U); break;
    case HMPY_MSG_UPLOAD_ABORT: hmpy_handle_upload_finish(frame, 1U); break;
    case HMPY_MSG_DELETE: hmpy_handle_delete(frame); break;
    case HMPY_MSG_SET_STARTUP: hmpy_handle_set_startup(frame); break;
    case HMPY_MSG_FORMAT: hmpy_handle_format(frame); break;
    case HMPY_MSG_RUN: hmpy_handle_run(frame); break;
    case HMPY_MSG_STOP: hmpy_handle_stop(frame); break;
    case HMPY_MSG_STATUS: hmpy_handle_status(frame); break;
    case HMPY_MSG_PING:
        (void)hmpy_response(frame->type, frame->request_id,
                            frame->payload, frame->payload_length, 0U);
        break;
    case HMPY_MSG_SESSION_CLOSE:
        if(frame->payload_length != 0U)
        {
            hmpy_error(frame->type, frame->request_id,
                       HMPY_ERROR_INVALID_PAYLOAD, "close payload");
            break;
        }
        (void)hmpy_response(frame->type, frame->request_id, NULL, 0U, 0U);
        hmpy_session_finish();
        break;
    default:
        hmpy_error(frame->type, frame->request_id,
                   HMPY_ERROR_UNSUPPORTED_TYPE, "unsupported type");
        break;
    }
}

static uint8_t hmpy_pump_runtime_state(void)
{
    micropython_runtime_status_t runtime;

    micropython_runtime_poll();
    micropython_runtime_get_status(&runtime);
    if(g_session.last_state == (uint8_t)runtime.state &&
       g_session.last_exit == (uint8_t)runtime.exit_reason &&
       g_session.last_run_id == runtime.run_id)
        return 0U;
    g_session.last_state = (uint8_t)runtime.state;
    g_session.last_exit = (uint8_t)runtime.exit_reason;
    if(g_session.last_run_id != runtime.run_id)
    {
        g_session.last_run_id = runtime.run_id;
        g_session.output_cursor = 0U;
    }
    g_payload[0] = (uint8_t)runtime.state;
    g_payload[1] = (uint8_t)runtime.exit_reason;
    hmpy_wr32(g_payload + 2U, runtime.run_id);
    hmpy_wr64(g_payload + 6U, runtime.heartbeat_us);
    (void)hmpy_send(HMPY_MSG_STATE, 0U, 0U, g_payload, 14U);
    return 1U;
}

static void hmpy_pump_runtime_output(void)
{
    micropython_runtime_status_t runtime;
    uint32_t lost = 0U;
    uint32_t sequence;
    size_t count;

    micropython_runtime_get_status(&runtime);
    count = micropython_runtime_read_output_since(
        &g_session.output_cursor, (char *)(g_payload + 8U),
        HMPY_EVENT_DATA_MAX, &lost);
    if(lost)
        hmpy_emit_dropped(1U, runtime.run_id, lost);
    if(!count)
        return;
    sequence = g_session.output_cursor - (uint32_t)count;
    hmpy_wr32(g_payload, runtime.run_id);
    hmpy_wr32(g_payload + 4U, sequence);
    (void)hmpy_send(HMPY_MSG_STDOUT, 0U, 0U, g_payload,
                    (uint32_t)count + 8U);
}

static void hmpy_pump_diagnostics(void)
{
    micropython_runtime_status_t runtime;
    uint32_t lost = 0U;
    uint32_t sequence;
    size_t count;

    micropython_runtime_get_status(&runtime);
    count = debug_console_read_diagnostics_since(
        &g_session.diagnostic_cursor, g_payload + 8U,
        HMPY_EVENT_DATA_MAX, &lost);
    if(lost)
        hmpy_emit_dropped(2U, runtime.run_id, lost);
    if(!count)
        return;
    sequence = g_session.diagnostic_cursor - (uint32_t)count;
    hmpy_wr32(g_payload, runtime.run_id);
    hmpy_wr32(g_payload + 4U, sequence);
    (void)hmpy_send(HMPY_MSG_STDERR, 0U, 0U, g_payload,
                    (uint32_t)count + 8U);
}

uint8_t hmpy_session_active(void)
{
    return g_session.active;
}

void hmpy_session_begin(void)
{
    if(g_session.active)
        return;
    memset(&g_session, 0, sizeof(g_session));
    hmpy_stream_decoder_init(&g_session.decoder);
    g_session.active = 1U;
    g_session.last_state = 0xFFU;
    g_session.last_exit = 0xFFU;
    g_session.last_receive_us = hal_time_us();
    g_session.diagnostic_cursor = debug_console_diagnostic_cursor();
    debug_console_write_text(HMPY_LINE_READY "\n");
    debug_console_set_framed_mode(1U);
    (void)userfs_mount();
}

void hmpy_session_tick(void)
{
    uint8_t byte;

    if(!g_session.active)
        return;
    if(hal_time_us() - g_session.last_receive_us >= HMPY_SESSION_LEASE_US)
    {
        hmpy_session_finish();
        return;
    }
    while(g_session.active && debug_console_read(&byte, 1U) == 1U)
    {
        hmpy_frame_t frame;
        hmpy_codec_status_t status = hmpy_stream_decoder_feed(
            &g_session.decoder, byte, &frame);

        if(status == HMPY_CODEC_FRAME_READY)
        {
            g_session.last_receive_us = hal_time_us();
            hmpy_handle_frame(&frame);
            /* Formatting/mounting can itself outlive the lease. Start the
             * next idle interval after the completed response, not before
             * executing a bounded request. */
            if(g_session.active)
                g_session.last_receive_us = hal_time_us();
        }
    }
    if(!g_session.active)
        return;
    if(g_session.read_active)
    {
        hmpy_pump_read();
        return;
    }
    if(hmpy_pump_runtime_state())
        return;
    hmpy_pump_runtime_output();
    hmpy_pump_diagnostics();
}
