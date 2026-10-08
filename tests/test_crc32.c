#include <stdio.h>
#include <string.h>
#include "crc32.h"

static int failures;

#define CHECK(cond)                                           \
    do {                                                      \
        if (!(cond)) {                                        \
            printf("FAIL line %d: %s\n", __LINE__, #cond);    \
            failures++;                                       \
        }                                                     \
    } while (0)

int main(void)
{
    const uint8_t *msg = (const uint8_t *)"123456789";

    CHECK(crc32_update(0, msg, 0) == 0x00000000u);
    CHECK(crc32_update(0, msg, 9) == 0xCBF43926u);

    uint32_t part = crc32_update(0, msg, 4);
    part = crc32_update(part, msg + 4, 5);
    CHECK(part == 0xCBF43926u);

    uint8_t flipped[9];
    memcpy(flipped, msg, 9);
    flipped[3] ^= 0x01u;
    CHECK(crc32_update(0, flipped, 9) != 0xCBF43926u);

    if (failures == 0) {
        printf("test_crc32: all checks passed\n");
    }
    return failures != 0;
}
