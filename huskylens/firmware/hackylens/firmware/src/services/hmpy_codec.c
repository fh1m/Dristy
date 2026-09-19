#include "hmpy_codec.h"

#include <string.h>

static uint16_t hmpy_read_u16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t hmpy_read_u32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static void hmpy_write_u16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void hmpy_write_u32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static uint8_t hmpy_type_is_known(uint8_t type)
{
    switch(type)
    {
        case HMPY_MSG_HELLO:
        case HMPY_MSG_LIST:
        case HMPY_MSG_STAT:
        case HMPY_MSG_READ:
        case HMPY_MSG_UPLOAD_BEGIN:
        case HMPY_MSG_UPLOAD_CHUNK:
        case HMPY_MSG_UPLOAD_COMMIT:
        case HMPY_MSG_UPLOAD_ABORT:
        case HMPY_MSG_DELETE:
        case HMPY_MSG_SET_STARTUP:
        case HMPY_MSG_FORMAT:
        case HMPY_MSG_RUN:
        case HMPY_MSG_STOP:
        case HMPY_MSG_STATUS:
        case HMPY_MSG_PING:
        case HMPY_MSG_SESSION_CLOSE:
        case HMPY_MSG_STDOUT:
        case HMPY_MSG_STDERR:
        case HMPY_MSG_STATE:
        case HMPY_MSG_FILE_CHANGED:
        case HMPY_MSG_DROPPED:
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t hmpy_type_is_event(uint8_t type)
{
    return type >= HMPY_MSG_STDOUT && type <= HMPY_MSG_DROPPED;
}

static hmpy_codec_status_t hmpy_validate_envelope(uint8_t type, uint16_t flags,
                                                   uint32_t request_id)
{
    if(!hmpy_type_is_known(type))
        return HMPY_CODEC_ERROR_UNKNOWN_TYPE;
    if(flags & (uint16_t)~HMPY_FLAG_MASK)
        return HMPY_CODEC_ERROR_RESERVED_FLAGS;

    if(hmpy_type_is_event(type))
    {
        if(flags != 0U || request_id != 0U)
            return HMPY_CODEC_ERROR_INVALID_ENVELOPE;
        return HMPY_CODEC_OK;
    }

    if(request_id == 0U)
        return HMPY_CODEC_ERROR_INVALID_ENVELOPE;
    if((flags & HMPY_FLAG_ERROR) && !(flags & HMPY_FLAG_RESPONSE))
        return HMPY_CODEC_ERROR_INVALID_ENVELOPE;
    if((flags & HMPY_FLAG_MORE) && !(flags & HMPY_FLAG_RESPONSE))
        return HMPY_CODEC_ERROR_INVALID_ENVELOPE;
    return HMPY_CODEC_OK;
}

uint32_t hmpy_crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFUL;
    size_t index;

    if(length && !data)
        return 0U;
    for(index = 0U; index < length; index++)
    {
        uint8_t bit;
        crc ^= data[index];
        for(bit = 0U; bit < 8U; bit++)
            crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1U)));
    }
    return ~crc;
}

const char *hmpy_codec_status_name(hmpy_codec_status_t status)
{
    switch(status)
    {
        case HMPY_CODEC_OK: return "ok";
        case HMPY_CODEC_INCOMPLETE: return "incomplete";
        case HMPY_CODEC_FRAME_READY: return "frame-ready";
        case HMPY_CODEC_ERROR_ARGUMENT: return "invalid-argument";
        case HMPY_CODEC_ERROR_OUTPUT_TOO_SMALL: return "output-too-small";
        case HMPY_CODEC_ERROR_COBS_MALFORMED: return "malformed-cobs";
        case HMPY_CODEC_ERROR_ENCODED_TOO_LARGE: return "encoded-too-large";
        case HMPY_CODEC_ERROR_FRAME_TOO_SHORT: return "frame-too-short";
        case HMPY_CODEC_ERROR_BAD_MAGIC: return "bad-magic";
        case HMPY_CODEC_ERROR_UNSUPPORTED_VERSION: return "unsupported-version";
        case HMPY_CODEC_ERROR_UNKNOWN_TYPE: return "unknown-type";
        case HMPY_CODEC_ERROR_RESERVED_FLAGS: return "reserved-flags";
        case HMPY_CODEC_ERROR_INVALID_ENVELOPE: return "invalid-envelope";
        case HMPY_CODEC_ERROR_PAYLOAD_TOO_LARGE: return "payload-too-large";
        case HMPY_CODEC_ERROR_LENGTH_MISMATCH: return "length-mismatch";
        case HMPY_CODEC_ERROR_CRC_MISMATCH: return "crc-mismatch";
        default: return "unknown-status";
    }
}

hmpy_codec_status_t hmpy_cobs_encode(const uint8_t *input, size_t input_length,
                                      uint8_t *output, size_t output_capacity,
                                      size_t *output_length)
{
    size_t read_index = 0U;
    size_t write_index = 1U;
    size_t code_index = 0U;
    uint8_t code = 1U;

    if(!output || !output_length || (input_length && !input))
        return HMPY_CODEC_ERROR_ARGUMENT;
    *output_length = 0U;
    if(output_capacity == 0U)
        return HMPY_CODEC_ERROR_OUTPUT_TOO_SMALL;

    while(read_index < input_length)
    {
        uint8_t value = input[read_index++];
        if(value == 0U)
        {
            output[code_index] = code;
            code = 1U;
            if(write_index >= output_capacity)
                return HMPY_CODEC_ERROR_OUTPUT_TOO_SMALL;
            code_index = write_index++;
        }
        else
        {
            if(write_index >= output_capacity)
                return HMPY_CODEC_ERROR_OUTPUT_TOO_SMALL;
            output[write_index++] = value;
            code++;
            if(code == 0xFFU)
            {
                output[code_index] = code;
                code = 1U;
                if(write_index >= output_capacity)
                    return HMPY_CODEC_ERROR_OUTPUT_TOO_SMALL;
                code_index = write_index++;
            }
        }
    }

    output[code_index] = code;
    *output_length = write_index;
    return HMPY_CODEC_OK;
}

hmpy_codec_status_t hmpy_cobs_decode(const uint8_t *input, size_t input_length,
                                      uint8_t *output, size_t output_capacity,
                                      size_t *output_length)
{
    size_t read_index = 0U;
    size_t write_index = 0U;

    if(!input || !output || !output_length)
        return HMPY_CODEC_ERROR_ARGUMENT;
    *output_length = 0U;
    if(input_length == 0U)
        return HMPY_CODEC_ERROR_COBS_MALFORMED;
    if(memchr(input, 0, input_length) != NULL)
        return HMPY_CODEC_ERROR_COBS_MALFORMED;

    while(read_index < input_length)
    {
        uint8_t code = input[read_index++];
        size_t copy_length;

        if(code == 0U)
            return HMPY_CODEC_ERROR_COBS_MALFORMED;
        copy_length = (size_t)code - 1U;
        if(copy_length > input_length - read_index)
            return HMPY_CODEC_ERROR_COBS_MALFORMED;
        if(copy_length > output_capacity - write_index)
            return HMPY_CODEC_ERROR_OUTPUT_TOO_SMALL;
        if(copy_length)
        {
            memcpy(output + write_index, input + read_index, copy_length);
            read_index += copy_length;
            write_index += copy_length;
        }
        if(code != 0xFFU && read_index < input_length)
        {
            if(write_index >= output_capacity)
                return HMPY_CODEC_ERROR_OUTPUT_TOO_SMALL;
            output[write_index++] = 0U;
        }
    }

    *output_length = write_index;
    return HMPY_CODEC_OK;
}

hmpy_codec_status_t hmpy_frame_encode(uint8_t type, uint16_t flags, uint32_t request_id,
                                       const uint8_t *payload, uint32_t payload_length,
                                       uint8_t *output, size_t output_capacity,
                                       size_t *output_length)
{
    uint8_t raw[HMPY_MAX_RAW_FRAME];
    size_t encoded_length = 0U;
    size_t raw_length;
    uint32_t crc;
    hmpy_codec_status_t status;

    if(!output || !output_length || (payload_length && !payload))
        return HMPY_CODEC_ERROR_ARGUMENT;
    *output_length = 0U;
    if(payload_length > HMPY_MAX_PAYLOAD_SIZE)
        return HMPY_CODEC_ERROR_PAYLOAD_TOO_LARGE;
    status = hmpy_validate_envelope(type, flags, request_id);
    if(status != HMPY_CODEC_OK)
        return status;
    if(output_capacity == 0U)
        return HMPY_CODEC_ERROR_OUTPUT_TOO_SMALL;

    raw[0] = 'H';
    raw[1] = 'M';
    raw[2] = 'P';
    raw[3] = 'Y';
    raw[4] = HMPY_PROTOCOL_VERSION;
    raw[5] = type;
    hmpy_write_u16(raw + 6U, flags);
    hmpy_write_u32(raw + 8U, request_id);
    hmpy_write_u32(raw + 12U, payload_length);
    if(payload_length)
        memcpy(raw + HMPY_HEADER_SIZE, payload, payload_length);
    raw_length = HMPY_HEADER_SIZE + (size_t)payload_length;
    crc = hmpy_crc32(raw, raw_length);
    hmpy_write_u32(raw + raw_length, crc);
    raw_length += HMPY_CRC_SIZE;

    status = hmpy_cobs_encode(raw, raw_length, output, output_capacity - 1U,
                              &encoded_length);
    if(status != HMPY_CODEC_OK)
        return status;
    if(encoded_length >= output_capacity)
        return HMPY_CODEC_ERROR_OUTPUT_TOO_SMALL;
    output[encoded_length] = 0U;
    *output_length = encoded_length + 1U;
    return HMPY_CODEC_OK;
}

hmpy_codec_status_t hmpy_frame_decode_raw(const uint8_t *raw, size_t raw_length,
                                           hmpy_frame_t *frame)
{
    uint32_t payload_length;
    size_t expected_length;
    size_t crc_offset;
    uint32_t expected_crc;
    hmpy_codec_status_t status;

    if(!raw || !frame)
        return HMPY_CODEC_ERROR_ARGUMENT;
    if(raw_length < HMPY_RAW_OVERHEAD)
        return HMPY_CODEC_ERROR_FRAME_TOO_SHORT;
    if(raw[0] != 'H' || raw[1] != 'M' || raw[2] != 'P' || raw[3] != 'Y')
        return HMPY_CODEC_ERROR_BAD_MAGIC;
    if(raw[4] != HMPY_PROTOCOL_VERSION)
        return HMPY_CODEC_ERROR_UNSUPPORTED_VERSION;

    payload_length = hmpy_read_u32(raw + 12U);
    if(payload_length > HMPY_MAX_PAYLOAD_SIZE)
        return HMPY_CODEC_ERROR_PAYLOAD_TOO_LARGE;
    expected_length = HMPY_RAW_OVERHEAD + (size_t)payload_length;
    if(raw_length != expected_length)
        return HMPY_CODEC_ERROR_LENGTH_MISMATCH;

    status = hmpy_validate_envelope(raw[5], hmpy_read_u16(raw + 6U),
                                    hmpy_read_u32(raw + 8U));
    if(status != HMPY_CODEC_OK)
        return status;

    crc_offset = HMPY_HEADER_SIZE + (size_t)payload_length;
    expected_crc = hmpy_read_u32(raw + crc_offset);
    if(expected_crc != hmpy_crc32(raw, crc_offset))
        return HMPY_CODEC_ERROR_CRC_MISMATCH;

    frame->type = raw[5];
    frame->flags = hmpy_read_u16(raw + 6U);
    frame->request_id = hmpy_read_u32(raw + 8U);
    frame->payload_length = payload_length;
    frame->payload = raw + HMPY_HEADER_SIZE;
    return HMPY_CODEC_OK;
}

hmpy_codec_status_t hmpy_frame_decode_packet(const uint8_t *packet, size_t packet_length,
                                              uint8_t *raw, size_t raw_capacity,
                                              hmpy_frame_t *frame)
{
    size_t raw_length = 0U;
    hmpy_codec_status_t status;

    if(!packet || !raw || !frame)
        return HMPY_CODEC_ERROR_ARGUMENT;
    if(packet_length > HMPY_MAX_ENCODED_PACKET)
        return HMPY_CODEC_ERROR_ENCODED_TOO_LARGE;
    status = hmpy_cobs_decode(packet, packet_length, raw, raw_capacity, &raw_length);
    if(status == HMPY_CODEC_ERROR_OUTPUT_TOO_SMALL && raw_capacity >= HMPY_MAX_RAW_FRAME)
        return HMPY_CODEC_ERROR_ENCODED_TOO_LARGE;
    if(status != HMPY_CODEC_OK)
        return status;
    if(raw_length > HMPY_MAX_RAW_FRAME)
        return HMPY_CODEC_ERROR_ENCODED_TOO_LARGE;
    return hmpy_frame_decode_raw(raw, raw_length, frame);
}

void hmpy_stream_decoder_init(hmpy_stream_decoder_t *decoder)
{
    if(decoder)
        memset(decoder, 0, sizeof(*decoder));
}

void hmpy_stream_decoder_reset(hmpy_stream_decoder_t *decoder)
{
    if(!decoder)
        return;
    decoder->encoded_length = 0U;
    decoder->last_error = HMPY_CODEC_OK;
    decoder->discarding = 0U;
}

static hmpy_codec_status_t hmpy_stream_error(hmpy_stream_decoder_t *decoder,
                                              hmpy_codec_status_t status)
{
    decoder->last_error = status;
    decoder->error_count++;
    return status;
}

hmpy_codec_status_t hmpy_stream_decoder_feed(hmpy_stream_decoder_t *decoder, uint8_t byte,
                                              hmpy_frame_t *frame)
{
    hmpy_codec_status_t status;

    if(!decoder || !frame)
        return HMPY_CODEC_ERROR_ARGUMENT;

    if(byte != 0U)
    {
        if(decoder->discarding)
            return HMPY_CODEC_INCOMPLETE;
        if(decoder->encoded_length >= HMPY_MAX_ENCODED_PACKET)
        {
            decoder->encoded_length = 0U;
            decoder->discarding = 1U;
            return HMPY_CODEC_INCOMPLETE;
        }
        decoder->encoded[decoder->encoded_length++] = byte;
        return HMPY_CODEC_INCOMPLETE;
    }

    if(decoder->discarding)
    {
        decoder->discarding = 0U;
        decoder->encoded_length = 0U;
        return hmpy_stream_error(decoder, HMPY_CODEC_ERROR_ENCODED_TOO_LARGE);
    }
    if(decoder->encoded_length == 0U)
        return HMPY_CODEC_INCOMPLETE;

    status = hmpy_frame_decode_packet(decoder->encoded, decoder->encoded_length,
                                      decoder->raw, sizeof(decoder->raw), frame);
    decoder->encoded_length = 0U;
    if(status != HMPY_CODEC_OK)
        return hmpy_stream_error(decoder, status);
    decoder->last_error = HMPY_CODEC_OK;
    return HMPY_CODEC_FRAME_READY;
}
