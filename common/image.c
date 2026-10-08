#include "image.h"
#include "sha256.h"

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

image_status_t image_verify(const uint8_t *slot, uint32_t slot_size, uint32_t *version_out)
{
    if (rd32(slot + IMAGE_OFF_MAGIC) != IMAGE_MAGIC) {
        return IMAGE_ERR_MAGIC;
    }
    uint32_t size = rd32(slot + IMAGE_OFF_SIZE);
    if (size == 0u || size > slot_size - IMAGE_HEADER_SIZE) {
        return IMAGE_ERR_SIZE;
    }

    sha256_ctx_t ctx;
    uint8_t digest[SHA256_DIGEST_SIZE];
    sha256_init(&ctx);
    sha256_update(&ctx, slot + IMAGE_HEADER_SIZE, size);
    sha256_final(&ctx, digest);

    uint8_t diff = 0;
    for (uint32_t i = 0; i < SHA256_DIGEST_SIZE; i++) {
        diff |= (uint8_t)(digest[i] ^ slot[IMAGE_OFF_HASH + i]);
    }
    if (diff != 0u) {
        return IMAGE_ERR_HASH;
    }

    if (version_out != 0) {
        *version_out = rd32(slot + IMAGE_OFF_VERSION);
    }
    return IMAGE_OK;
}
