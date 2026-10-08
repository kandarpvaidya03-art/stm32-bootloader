#include "image_sig.h"
#include "image.h"
#include "sha256.h"
#include "uECC.h"

int image_signature_ok(const uint8_t *slot, const uint8_t *public_key)
{
    sha256_ctx_t ctx;
    uint8_t digest[SHA256_DIGEST_SIZE];

    sha256_init(&ctx);
    sha256_update(&ctx, slot, IMAGE_SIGNED_SIZE);
    sha256_final(&ctx, digest);

    return uECC_verify(public_key, digest, sizeof digest, slot + IMAGE_OFF_SIG,
                       uECC_secp256r1()) == 1;
}
