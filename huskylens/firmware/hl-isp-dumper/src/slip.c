/* SLIP framing derived from loboris/ktool (Apache-2.0). */
#include "slip.h"

#include <uart.h>

#define UART_PORT UART_DEVICE_3
#define SLIP_END 0xC0U
#define SLIP_ESC 0xDBU
#define SLIP_ESC_END 0xDCU
#define SLIP_ESC_ESC 0xDDU

static void send_byte(uint8_t byte)
{
    uart_send_data(UART_PORT, (const char *)&byte, 1U);
}

int slip_decode_byte(slip_decoder_t *decoder, uint8_t byte,
                     uint8_t *output, size_t capacity)
{
    if(byte == SLIP_END)
    {
        if(decoder->state == SLIP_OUTSIDE)
        {
            decoder->state = SLIP_FRAME;
            decoder->length = 0U;
            decoder->overflow = 0U;
            return 0;
        }

        decoder->state = SLIP_OUTSIDE;
        return decoder->length != 0U && !decoder->overflow;
    }

    if(decoder->state == SLIP_OUTSIDE)
        return 0;

    if(decoder->state == SLIP_ESCAPE)
    {
        decoder->state = SLIP_FRAME;
        if(byte == SLIP_ESC_END)
            byte = SLIP_END;
        else if(byte == SLIP_ESC_ESC)
            byte = SLIP_ESC;
        else
        {
            decoder->overflow = 1U;
            return 0;
        }
    }
    else if(byte == SLIP_ESC)
    {
        decoder->state = SLIP_ESCAPE;
        return 0;
    }

    if(decoder->length < capacity)
        output[decoder->length++] = byte;
    else
        decoder->overflow = 1U;
    return 0;
}

void slip_send(const void *data, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    send_byte(SLIP_END);
    for(size_t i = 0; i < length; ++i)
    {
        if(bytes[i] == SLIP_END)
        {
            send_byte(SLIP_ESC);
            send_byte(SLIP_ESC_END);
        }
        else if(bytes[i] == SLIP_ESC)
        {
            send_byte(SLIP_ESC);
            send_byte(SLIP_ESC_ESC);
        }
        else
            send_byte(bytes[i]);
    }
    send_byte(SLIP_END);
}
