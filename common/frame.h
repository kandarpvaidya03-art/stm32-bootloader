#ifndef FRAME_H
#define FRAME_H

#include <stddef.h>
#include <stdint.h>

#define FRAME_SOF         0xA5u
#define FRAME_MAX_PAYLOAD 256u
#define FRAME_OVERHEAD    8u /* start + type + 2 length + 4 CRC */

typedef enum {
    FRAME_INCOMPLETE = 0, /* need more bytes */
    FRAME_READY,          /* type, len and payload are valid */
    FRAME_ERR_LENGTH,     /* length field larger than the maximum */
    FRAME_ERR_CRC         /* checksum mismatch */
} frame_result_t;

typedef struct {
    uint8_t state;
    uint8_t type;
    uint16_t len;
    uint16_t index;
    uint32_t crc_rx;
    uint8_t payload[FRAME_MAX_PAYLOAD];
} frame_parser_t;

void frame_parser_reset(frame_parser_t *p);
frame_result_t frame_parser_feed(frame_parser_t *p, uint8_t byte);

/* Builds a frame into out. Returns its total length, or 0 if it does not fit. */
size_t frame_encode(uint8_t type, const uint8_t *payload, uint16_t len,
                    uint8_t *out, size_t out_size);

#endif
