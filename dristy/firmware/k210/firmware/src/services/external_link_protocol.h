#ifndef HK_EXTERNAL_LINK_PROTOCOL_H
#define HK_EXTERNAL_LINK_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define HK_LINK_PROTOCOL_VERSION 1U
#define HK_LINK_MAX_PAYLOAD 160U
#define HK_LINK_FRAME_OVERHEAD 10U
#define HK_LINK_MAX_FRAME (HK_LINK_MAX_PAYLOAD + HK_LINK_FRAME_OVERHEAD)

typedef enum
{
    HK_LINK_GET_INFO = 0x01,
    HK_LINK_GET_RESULTS = 0x02,
    HK_LINK_PING = 0x03,
    HK_LINK_INFO = 0x81,
    HK_LINK_RESULTS = 0x82,
    HK_LINK_PONG = 0x83,
    HK_LINK_ERROR = 0xFF,
} hk_link_message_type_t;

typedef struct
{
    uint8_t type;
    uint16_t sequence;
    uint16_t payload_length;
    const uint8_t *payload;
} hk_link_message_t;

typedef struct
{
    uint8_t data[HK_LINK_MAX_FRAME];
    uint16_t length;
} hk_link_stream_parser_t;

uint16_t hk_link_crc16(const uint8_t *data, size_t length);
size_t hk_link_frame_encode(uint8_t type, uint16_t sequence, const uint8_t *payload,
                            uint16_t payload_length, uint8_t *output, size_t capacity);
uint8_t hk_link_frame_decode(const uint8_t *frame, size_t length, hk_link_message_t *message);
void hk_link_stream_reset(hk_link_stream_parser_t *parser);
uint8_t hk_link_stream_feed(hk_link_stream_parser_t *parser, uint8_t byte, hk_link_message_t *message);

#endif
