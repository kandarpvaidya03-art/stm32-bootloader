#ifndef CRC32_H
#define CRC32_H

#include <stddef.h>
#include <stdint.h>

/* Standard CRC-32 (same as zlib). Start with crc = 0; pass the previous
 * result back in to continue over more data. */
uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len);

#endif
