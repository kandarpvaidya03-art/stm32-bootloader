#ifndef FLASH_H
#define FLASH_H

#include <stdint.h>

void flash_unlock(void);
void flash_lock(void);
int flash_erase_sector(uint32_t sector);              /* 0 = ok, -1 = error */
int flash_write_word(uint32_t addr, uint32_t value);  /* 0 = ok, -1 = error */

#endif
