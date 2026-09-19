#include "external_link_protocol.h"

#include <string.h>

static uint16_t read_u16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static void write_u16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

uint16_t hk_link_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFU;
    for(size_t i = 0; i < length; i++)
    {
        crc ^= (uint16_t)data[i] << 8;
        for(uint8_t bit = 0; bit < 8U; bit++)
            crc = (crc & 0x8000U) ? (uint16_t)((crc << 1) ^ 0x1021U) : (uint16_t)(crc << 1);
    }
    return crc;
}

size_t hk_link_frame_encode(uint8_t type, uint16_t sequence, const uint8_t *payload,
                            uint16_t payload_length, uint8_t *output, size_t capacity)
{
    size_t length = (size_t)payload_length + HK_LINK_FRAME_OVERHEAD;
    uint16_t crc;

    if(!output || payload_length > HK_LINK_MAX_PAYLOAD || capacity < length ||
       (payload_length && !payload))
        return 0U;
    output[0] = 'H';
    output[1] = 'K';
    output[2] = HK_LINK_PROTOCOL_VERSION;
    output[3] = type;
    write_u16(output + 4, sequence);
    write_u16(output + 6, payload_length);
    if(payload_length)
        memcpy(output + 8, payload, payload_length);
    crc = hk_link_crc16(output + 2, (size_t)payload_length + 6U);
    write_u16(output + 8U + payload_length, crc);
    return length;
}

uint8_t hk_link_frame_decode(const uint8_t *frame, size_t length, hk_link_message_t *message)
{
    uint16_t payload_length;
    uint16_t expected_crc;

    if(!frame || !message || length < HK_LINK_FRAME_OVERHEAD || frame[0] != 'H' || frame[1] != 'K' ||
       frame[2] != HK_LINK_PROTOCOL_VERSION)
        return 0U;
    payload_length = read_u16(frame + 6);
    if(payload_length > HK_LINK_MAX_PAYLOAD || length != (size_t)payload_length + HK_LINK_FRAME_OVERHEAD)
        return 0U;
    expected_crc = read_u16(frame + 8U + payload_length);
    if(expected_crc != hk_link_crc16(frame + 2, (size_t)payload_length + 6U))
        return 0U;
    message->type = frame[3];
    message->sequence = read_u16(frame + 4);
    message->payload_length = payload_length;
    message->payload = frame + 8;
    return 1U;
}

void hk_link_stream_reset(hk_link_stream_parser_t *parser)
{
    if(parser)
        parser->length = 0U;
}

uint8_t hk_link_stream_feed(hk_link_stream_parser_t *parser, uint8_t byte, hk_link_message_t *message)
{
    uint16_t expected;

    if(!parser || !message)
        return 0U;
    if(parser->length == 0U && byte != 'H')
        return 0U;
    if(parser->length == 1U && byte != 'K')
    {
        parser->data[0] = byte;
        parser->length = byte == 'H' ? 1U : 0U;
        return 0U;
    }
    if(parser->length >= HK_LINK_MAX_FRAME)
        parser->length = 0U;
    parser->data[parser->length++] = byte;
    if(parser->length < 8U)
        return 0U;
    expected = (uint16_t)(read_u16(parser->data + 6) + HK_LINK_FRAME_OVERHEAD);
    if(expected > HK_LINK_MAX_FRAME)
    {
        parser->length = 0U;
        return 0U;
    }
    if(parser->length < expected)
        return 0U;
    if(parser->length == expected && hk_link_frame_decode(parser->data, expected, message))
    {
        parser->length = 0U;
        return 1U;
    }
    parser->length = 0U;
    return 0U;
}
