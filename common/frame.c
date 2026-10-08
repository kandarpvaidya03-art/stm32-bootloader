#include "frame.h"
#include "crc32.h"

enum { ST_SOF, ST_TYPE, ST_LEN_LO, ST_LEN_HI, ST_PAYLOAD, ST_CRC };

void frame_parser_reset(frame_parser_t *p)
{
    p->state = ST_SOF;
    p->type = 0;
    p->len = 0;
    p->index = 0;
    p->crc_rx = 0;
}

frame_result_t frame_parser_feed(frame_parser_t *p, uint8_t byte)
{
    switch (p->state) {
    case ST_SOF:
        if (byte == FRAME_SOF) {
            p->state = ST_TYPE;
        }
        break;

    case ST_TYPE:
        p->type = byte;
        p->state = ST_LEN_LO;
        break;

    case ST_LEN_LO:
        p->len = byte;
        p->state = ST_LEN_HI;
        break;

    case ST_LEN_HI:
        p->len |= (uint16_t)((uint16_t)byte << 8);
        if (p->len > FRAME_MAX_PAYLOAD) {
            p->state = ST_SOF;
            return FRAME_ERR_LENGTH;
        }
        p->index = 0;
        p->crc_rx = 0;
        p->state = (p->len != 0u) ? ST_PAYLOAD : ST_CRC;
        break;

    case ST_PAYLOAD:
        p->payload[p->index++] = byte;
        if (p->index == p->len) {
            p->index = 0;
            p->state = ST_CRC;
        }
        break;

    case ST_CRC:
        p->crc_rx |= (uint32_t)byte << (8u * p->index);
        p->index++;
        if (p->index == 4u) {
            const uint8_t header[3] = {p->type, (uint8_t)(p->len & 0xFFu),
                                       (uint8_t)(p->len >> 8)};
            uint32_t crc = crc32_update(0, header, sizeof header);
            crc = crc32_update(crc, p->payload, p->len);
            p->state = ST_SOF;
            return (crc == p->crc_rx) ? FRAME_READY : FRAME_ERR_CRC;
        }
        break;

    default:
        p->state = ST_SOF;
        break;
    }
    return FRAME_INCOMPLETE;
}

size_t frame_encode(uint8_t type, const uint8_t *payload, uint16_t len,
                    uint8_t *out, size_t out_size)
{
    if (len > FRAME_MAX_PAYLOAD || out_size < (size_t)len + FRAME_OVERHEAD) {
        return 0;
    }
    out[0] = FRAME_SOF;
    out[1] = type;
    out[2] = (uint8_t)(len & 0xFFu);
    out[3] = (uint8_t)(len >> 8);
    for (uint16_t i = 0; i < len; i++) {
        out[4u + i] = payload[i];
    }
    uint32_t crc = crc32_update(0, &out[1], 3u + (size_t)len);
    out[4u + len] = (uint8_t)(crc & 0xFFu);
    out[5u + len] = (uint8_t)((crc >> 8) & 0xFFu);
    out[6u + len] = (uint8_t)((crc >> 16) & 0xFFu);
    out[7u + len] = (uint8_t)((crc >> 24) & 0xFFu);
    return (size_t)len + FRAME_OVERHEAD;
}
