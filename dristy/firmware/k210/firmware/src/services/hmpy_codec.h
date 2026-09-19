#ifndef HMPY_CODEC_H
#define HMPY_CODEC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HMPY_LINE_HANDSHAKE "HKMPROTO 1"
#define HMPY_LINE_READY "HKMPROTO 1 READY"

#define HMPY_PROTOCOL_VERSION 1U
#define HMPY_HEADER_SIZE 16U
#define HMPY_CRC_SIZE 4U
#define HMPY_RAW_OVERHEAD (HMPY_HEADER_SIZE + HMPY_CRC_SIZE)
#define HMPY_MAX_PAYLOAD_SIZE 1024U
#define HMPY_MAX_RAW_FRAME (HMPY_RAW_OVERHEAD + HMPY_MAX_PAYLOAD_SIZE)
#define HMPY_MAX_ENCODED_PACKET (HMPY_MAX_RAW_FRAME + (HMPY_MAX_RAW_FRAME / 254U) + 1U)
#define HMPY_MAX_WIRE_FRAME (HMPY_MAX_ENCODED_PACKET + 1U)

typedef enum
{
    HMPY_MSG_HELLO = 0x01,
    HMPY_MSG_LIST = 0x02,
    HMPY_MSG_STAT = 0x03,
    HMPY_MSG_READ = 0x04,

    HMPY_MSG_UPLOAD_BEGIN = 0x10,
    HMPY_MSG_UPLOAD_CHUNK = 0x11,
    HMPY_MSG_UPLOAD_COMMIT = 0x12,
    HMPY_MSG_UPLOAD_ABORT = 0x13,

    HMPY_MSG_DELETE = 0x20,
    HMPY_MSG_SET_STARTUP = 0x21,
    HMPY_MSG_FORMAT = 0x22,

    HMPY_MSG_RUN = 0x30,
    HMPY_MSG_STOP = 0x31,
    HMPY_MSG_STATUS = 0x32,

    HMPY_MSG_PING = 0x40,
    HMPY_MSG_SESSION_CLOSE = 0x41,

    HMPY_MSG_STDOUT = 0x80,
    HMPY_MSG_STDERR = 0x81,
    HMPY_MSG_STATE = 0x82,
    HMPY_MSG_FILE_CHANGED = 0x83,
    HMPY_MSG_DROPPED = 0x84,
} hmpy_message_type_t;

typedef enum
{
    HMPY_FLAG_RESPONSE = 0x0001,
    HMPY_FLAG_ERROR = 0x0002,
    HMPY_FLAG_MORE = 0x0004,
    HMPY_FLAG_MASK = HMPY_FLAG_RESPONSE | HMPY_FLAG_ERROR | HMPY_FLAG_MORE,
} hmpy_frame_flag_t;

/* Stable error values carried by an ERROR response payload. */
typedef enum
{
    HMPY_ERROR_OK = 0,
    HMPY_ERROR_INVALID_REQUEST = 1,
    HMPY_ERROR_UNSUPPORTED_VERSION = 2,
    HMPY_ERROR_UNSUPPORTED_TYPE = 3,
    HMPY_ERROR_INVALID_PAYLOAD = 4,
    HMPY_ERROR_NOT_FOUND = 5,
    HMPY_ERROR_ALREADY_EXISTS = 6,
    HMPY_ERROR_BUSY = 7,
    HMPY_ERROR_PERMISSION_DENIED = 8,
    HMPY_ERROR_NO_SPACE = 9,
    HMPY_ERROR_IO = 10,
    HMPY_ERROR_CRC_MISMATCH = 11,
    HMPY_ERROR_OFFSET_MISMATCH = 12,
    HMPY_ERROR_LIMIT_EXCEEDED = 13,
    HMPY_ERROR_NOT_RUNNING = 14,
    HMPY_ERROR_TIMEOUT = 15,
    HMPY_ERROR_INTERNAL = 16,
    HMPY_ERROR_CONFIRMATION_REQUIRED = 17,
    HMPY_ERROR_SESSION_EXPIRED = 18,
} hmpy_error_code_t;

/* Codec statuses are also mirrored by tools/hmpy_protocol.py. */
typedef enum
{
    HMPY_CODEC_OK = 0,
    HMPY_CODEC_INCOMPLETE = 1,
    HMPY_CODEC_FRAME_READY = 2,

    HMPY_CODEC_ERROR_ARGUMENT = 16,
    HMPY_CODEC_ERROR_OUTPUT_TOO_SMALL = 17,
    HMPY_CODEC_ERROR_COBS_MALFORMED = 18,
    HMPY_CODEC_ERROR_ENCODED_TOO_LARGE = 19,
    HMPY_CODEC_ERROR_FRAME_TOO_SHORT = 20,
    HMPY_CODEC_ERROR_BAD_MAGIC = 21,
    HMPY_CODEC_ERROR_UNSUPPORTED_VERSION = 22,
    HMPY_CODEC_ERROR_UNKNOWN_TYPE = 23,
    HMPY_CODEC_ERROR_RESERVED_FLAGS = 24,
    HMPY_CODEC_ERROR_INVALID_ENVELOPE = 25,
    HMPY_CODEC_ERROR_PAYLOAD_TOO_LARGE = 26,
    HMPY_CODEC_ERROR_LENGTH_MISMATCH = 27,
    HMPY_CODEC_ERROR_CRC_MISMATCH = 28,
} hmpy_codec_status_t;

typedef struct
{
    uint8_t type;
    uint16_t flags;
    uint32_t request_id;
    uint32_t payload_length;
    const uint8_t *payload;
} hmpy_frame_t;

/* Fixed-storage streaming decoder. A decoded payload remains valid until the
 * next complete packet is processed or the decoder is reset. */
typedef struct
{
    uint8_t encoded[HMPY_MAX_ENCODED_PACKET];
    uint8_t raw[HMPY_MAX_RAW_FRAME];
    size_t encoded_length;
    uint32_t error_count;
    hmpy_codec_status_t last_error;
    uint8_t discarding;
} hmpy_stream_decoder_t;

uint32_t hmpy_crc32(const uint8_t *data, size_t length);
const char *hmpy_codec_status_name(hmpy_codec_status_t status);

hmpy_codec_status_t hmpy_cobs_encode(const uint8_t *input, size_t input_length,
                                      uint8_t *output, size_t output_capacity,
                                      size_t *output_length);
hmpy_codec_status_t hmpy_cobs_decode(const uint8_t *input, size_t input_length,
                                      uint8_t *output, size_t output_capacity,
                                      size_t *output_length);

/* Encodes one complete COBS packet and appends its 0x00 delimiter. */
hmpy_codec_status_t hmpy_frame_encode(uint8_t type, uint16_t flags, uint32_t request_id,
                                       const uint8_t *payload, uint32_t payload_length,
                                       uint8_t *output, size_t output_capacity,
                                       size_t *output_length);

/* Decodes an already de-COBSed frame. The payload points into raw. */
hmpy_codec_status_t hmpy_frame_decode_raw(const uint8_t *raw, size_t raw_length,
                                           hmpy_frame_t *frame);

/* Decodes one COBS packet without its 0x00 delimiter. */
hmpy_codec_status_t hmpy_frame_decode_packet(const uint8_t *packet, size_t packet_length,
                                              uint8_t *raw, size_t raw_capacity,
                                              hmpy_frame_t *frame);

void hmpy_stream_decoder_init(hmpy_stream_decoder_t *decoder);
void hmpy_stream_decoder_reset(hmpy_stream_decoder_t *decoder);
hmpy_codec_status_t hmpy_stream_decoder_feed(hmpy_stream_decoder_t *decoder, uint8_t byte,
                                              hmpy_frame_t *frame);

#ifdef __cplusplus
}
#endif

#endif
