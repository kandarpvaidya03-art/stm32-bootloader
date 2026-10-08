#ifndef IMAGE_SIG_H
#define IMAGE_SIG_H

#include <stdint.h>

#define IMAGE_SIGNED_SIZE   48u /* header bytes covered by the signature */
#define IMAGE_PUBLIC_KEY_SIZE 64u /* P-256 public key: X then Y, big-endian */

/* Returns 1 if the header's signature is valid for public_key, 0 otherwise. */
int image_signature_ok(const uint8_t *slot, const uint8_t *public_key);

#endif
