#include <stdio.h>
#include <string.h>
#include "image.h"
#include "image_sig.h"
#include "sha256.h"
#include "uECC.h"

static int failures;

#define CHECK(cond)                                           \
    do {                                                      \
        if (!(cond)) {                                        \
            printf("FAIL line %d: %s\n", __LINE__, #cond);    \
            failures++;                                       \
        }                                                     \
    } while (0)

static uint8_t header[IMAGE_HEADER_SIZE];

static void sign_header(const uint8_t *private_key)
{
    sha256_ctx_t ctx;
    uint8_t digest[SHA256_DIGEST_SIZE];

    sha256_init(&ctx);
    sha256_update(&ctx, header, IMAGE_SIGNED_SIZE);
    sha256_final(&ctx, digest);
    CHECK(uECC_sign(private_key, digest, sizeof digest, header + IMAGE_OFF_SIG,
                    uECC_secp256r1()) == 1);
}

int main(void)
{
    uint8_t priv_a[32], pub_a[64], priv_b[32], pub_b[64];

    CHECK(uECC_make_key(pub_a, priv_a, uECC_secp256r1()) == 1);
    CHECK(uECC_make_key(pub_b, priv_b, uECC_secp256r1()) == 1);

    memset(header, 0xFF, sizeof header);
    for (uint32_t i = 0; i < IMAGE_SIGNED_SIZE; i++) {
        header[i] = (uint8_t)(i * 11u + 3u);
    }
    sign_header(priv_a);

    /* untouched header, correct key */
    CHECK(image_signature_ok(header, pub_a) == 1);

    /* changed version field */
    header[IMAGE_OFF_VERSION] ^= 0x01u;
    CHECK(image_signature_ok(header, pub_a) == 0);
    header[IMAGE_OFF_VERSION] ^= 0x01u;

    /* changed code hash */
    header[IMAGE_OFF_HASH + 10] ^= 0x40u;
    CHECK(image_signature_ok(header, pub_a) == 0);
    header[IMAGE_OFF_HASH + 10] ^= 0x40u;

    /* changed signature */
    header[IMAGE_OFF_SIG + 5] ^= 0x01u;
    CHECK(image_signature_ok(header, pub_a) == 0);
    header[IMAGE_OFF_SIG + 5] ^= 0x01u;

    /* still valid after restoring everything */
    CHECK(image_signature_ok(header, pub_a) == 1);

    /* signed by someone else's key */
    CHECK(image_signature_ok(header, pub_b) == 0);

    /* missing signature: all zeros, and erased flash */
    memset(header + IMAGE_OFF_SIG, 0x00, 64);
    CHECK(image_signature_ok(header, pub_a) == 0);
    memset(header + IMAGE_OFF_SIG, 0xFF, 64);
    CHECK(image_signature_ok(header, pub_a) == 0);

    if (failures == 0) {
        printf("test_image_sig: all checks passed\n");
    }
    return failures != 0;
}
