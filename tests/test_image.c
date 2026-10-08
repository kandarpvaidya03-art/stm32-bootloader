#include <stdio.h>
#include <string.h>
#include "image.h"
#include "sha256.h"

static int failures;

#define CHECK(cond)                                           \
    do {                                                      \
        if (!(cond)) {                                        \
            printf("FAIL line %d: %s\n", __LINE__, #cond);    \
            failures++;                                       \
        }                                                     \
    } while (0)

static uint8_t slot[2048];

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void build(uint32_t version, uint32_t size)
{
    sha256_ctx_t ctx;

    memset(slot, 0xFF, sizeof slot);
    wr32(slot + IMAGE_OFF_MAGIC, IMAGE_MAGIC);
    wr32(slot + IMAGE_OFF_VERSION, version);
    wr32(slot + IMAGE_OFF_SIZE, size);
    wr32(slot + 12, 0);
    for (uint32_t i = 0; i < size; i++) {
        slot[IMAGE_HEADER_SIZE + i] = (uint8_t)(i * 3u + 5u);
    }
    sha256_init(&ctx);
    sha256_update(&ctx, slot + IMAGE_HEADER_SIZE, size);
    sha256_final(&ctx, slot + IMAGE_OFF_HASH);
}

int main(void)
{
    uint32_t version = 0;
    const uint32_t max_size = sizeof slot - IMAGE_HEADER_SIZE;

    build(7, 100);
    CHECK(image_verify(slot, sizeof slot, &version) == IMAGE_OK);
    CHECK(version == 7);
    CHECK(image_verify(slot, sizeof slot, 0) == IMAGE_OK);

    build(1, max_size);
    CHECK(image_verify(slot, sizeof slot, 0) == IMAGE_OK);

    build(7, 100);
    slot[IMAGE_HEADER_SIZE + 50] ^= 0x01u; /* bit flip in the code */
    CHECK(image_verify(slot, sizeof slot, 0) == IMAGE_ERR_HASH);

    build(7, 100);
    slot[IMAGE_OFF_HASH + 4] ^= 0x80u; /* bit flip in the stored hash */
    CHECK(image_verify(slot, sizeof slot, 0) == IMAGE_ERR_HASH);

    build(7, 100);
    slot[0] ^= 0x01u;
    CHECK(image_verify(slot, sizeof slot, 0) == IMAGE_ERR_MAGIC);

    build(7, 100);
    wr32(slot + IMAGE_OFF_SIZE, 0);
    CHECK(image_verify(slot, sizeof slot, 0) == IMAGE_ERR_SIZE);

    build(7, 100);
    wr32(slot + IMAGE_OFF_SIZE, max_size + 1u);
    CHECK(image_verify(slot, sizeof slot, 0) == IMAGE_ERR_SIZE);

    memset(slot, 0xFF, sizeof slot); /* erased flash */
    CHECK(image_verify(slot, sizeof slot, 0) == IMAGE_ERR_MAGIC);

    if (failures == 0) {
        printf("test_image: all checks passed\n");
    }
    return failures != 0;
}
