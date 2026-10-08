#ifndef IMAGE_H
#define IMAGE_H

#include <stdint.h>

#define IMAGE_MAGIC       0x31495746u /* "FWI1" */
#define IMAGE_HEADER_SIZE 512u

#define IMAGE_OFF_MAGIC   0u
#define IMAGE_OFF_VERSION 4u
#define IMAGE_OFF_SIZE    8u
#define IMAGE_OFF_HASH    16u
#define IMAGE_OFF_SIG     48u

typedef enum {
    IMAGE_OK = 0,
    IMAGE_ERR_MAGIC = 1,
    IMAGE_ERR_SIZE = 2,
    IMAGE_ERR_HASH = 3
} image_status_t;

/* Checks the image at the start of a slot. version_out may be 0. */
image_status_t image_verify(const uint8_t *slot, uint32_t slot_size, uint32_t *version_out);

#endif
