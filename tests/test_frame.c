#include <stdio.h>
#include <string.h>
#include "frame.h"

static int failures;

#define CHECK(cond)                                           \
    do {                                                      \
        if (!(cond)) {                                        \
            printf("FAIL line %d: %s\n", __LINE__, #cond);    \
            failures++;                                       \
        }                                                     \
    } while (0)

static frame_result_t feed_all(frame_parser_t *p, const uint8_t *buf, size_t n)
{
    frame_result_t last = FRAME_INCOMPLETE;
    for (size_t i = 0; i < n; i++) {
        frame_result_t r = frame_parser_feed(p, buf[i]);
        if (r != FRAME_INCOMPLETE) {
            last = r;
        }
    }
    return last;
}

int main(void)
{
    static frame_parser_t p;
    uint8_t buf[FRAME_MAX_PAYLOAD + FRAME_OVERHEAD];
    uint8_t data[FRAME_MAX_PAYLOAD];
    size_t n;

    for (size_t i = 0; i < sizeof data; i++) {
        data[i] = (uint8_t)(i * 7u + 1u);
    }
    frame_parser_reset(&p);

    /* full-size round trip */
    n = frame_encode(0x10, data, 256, buf, sizeof buf);
    CHECK(n == 256u + FRAME_OVERHEAD);
    CHECK(feed_all(&p, buf, n) == FRAME_READY);
    CHECK(p.type == 0x10);
    CHECK(p.len == 256);
    CHECK(memcmp(p.payload, data, 256) == 0);

    /* empty payload */
    n = frame_encode(0x01, NULL, 0, buf, sizeof buf);
    CHECK(n == FRAME_OVERHEAD);
    CHECK(feed_all(&p, buf, n) == FRAME_READY);
    CHECK(p.type == 0x01);
    CHECK(p.len == 0);

    /* one flipped bit in the payload is rejected */
    n = frame_encode(0x10, data, 32, buf, sizeof buf);
    buf[10] ^= 0x04u;
    CHECK(feed_all(&p, buf, n) == FRAME_ERR_CRC);

    /* parser recovers and accepts the next good frame */
    n = frame_encode(0x10, data, 32, buf, sizeof buf);
    CHECK(feed_all(&p, buf, n) == FRAME_READY);

    /* length field of 257 is rejected */
    const uint8_t oversize[4] = {FRAME_SOF, 0x10, 0x01, 0x01};
    CHECK(feed_all(&p, oversize, sizeof oversize) == FRAME_ERR_LENGTH);

    /* noise before the start byte is skipped */
    uint8_t noisy[3 + 8 + FRAME_OVERHEAD] = {0x00, 0xFF, 0x33};
    n = frame_encode(0x10, data, 8, noisy + 3, sizeof noisy - 3);
    CHECK(n == 8u + FRAME_OVERHEAD);
    CHECK(feed_all(&p, noisy, n + 3) == FRAME_READY);
    CHECK(p.len == 8);

    /* truncated frame never reports ready */
    n = frame_encode(0x10, data, 32, buf, sizeof buf);
    frame_parser_reset(&p);
    CHECK(feed_all(&p, buf, n - 3) == FRAME_INCOMPLETE);
    frame_parser_reset(&p);
    CHECK(feed_all(&p, buf, n) == FRAME_READY);

    /* output buffer too small */
    CHECK(frame_encode(0x10, data, 32, buf, 10) == 0);

    if (failures == 0) {
        printf("test_frame: all checks passed\n");
    }
    return failures != 0;
}
