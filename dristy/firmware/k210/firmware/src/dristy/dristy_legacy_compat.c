#include "dristy_legacy_compat.h"

#include <string.h>

#include "dristy_protocol.h"
#include "dristy_result_bus.h"

#define HUSKY_CMD_KNOCK           0x2C
#define HUSKY_CMD_RETURN_OK       0x2E
#define HUSKY_CMD_REQUEST         0x20
#define HUSKY_CMD_REQUEST_BLOCKS  0x21

static void put_u16(uint8_t *buf, uint16_t v)
{
    buf[0] = (uint8_t)(v & 0xFF);
    buf[1] = (uint8_t)(v >> 8);
}

int dristy_legacy_compat_handle(uint8_t command,
                               const uint8_t *data, uint8_t data_len,
                               uint8_t *response_cmd,
                               uint8_t *response_buf, uint8_t *response_len)
{
    (void)data;
    (void)data_len;

    if(command < 0x20 || command > 0x3E)
        return 0;

    switch(command)
    {
    case HUSKY_CMD_KNOCK:
        *response_cmd = HUSKY_CMD_RETURN_OK;
        *response_len = 0;
        return 1;

    case HUSKY_CMD_REQUEST:
    case HUSKY_CMD_REQUEST_BLOCKS:
    {
        const dristy_result_snapshot_t *snap = dristy_result_bus_read();
        uint8_t off = 0;
        if(snap && snap->track_count > 0)
        {
            const dristy_result_track_t *t = &snap->tracks[0];
            put_u16(response_buf + off, t->id);
            off += 2;
            put_u16(response_buf + off, (uint16_t)(t->cx - t->w / 2));
            off += 2;
            put_u16(response_buf + off, (uint16_t)(t->cy - t->h / 2));
            off += 2;
            put_u16(response_buf + off, (uint16_t)(t->cx + t->w / 2));
            off += 2;
            put_u16(response_buf + off, (uint16_t)(t->cy + t->h / 2));
            off += 2;
            *response_cmd = 0x2A; /* RETURN_BLOCK */
        }
        else
        {
            *response_cmd = HUSKY_CMD_RETURN_OK;
        }
        *response_len = off;
        return 1;
    }

    default:
        *response_cmd = HUSKY_CMD_RETURN_OK;
        *response_len = 0;
        return 1;
    }
}
