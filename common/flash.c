#include "flash.h"

#define FLASH_KEYR (*(volatile uint32_t *)0x40023C04u)
#define FLASH_SR   (*(volatile uint32_t *)0x40023C0Cu)
#define FLASH_CR   (*(volatile uint32_t *)0x40023C10u)

#define SR_BSY      (1u << 16)
#define SR_ERRORS   0xF2u       /* sequence, parallelism, alignment, write-protect, operation */
#define CR_PG       (1u << 0)
#define CR_SER      (1u << 1)
#define CR_SNB_MASK (0xFu << 3)
#define CR_PSIZE_32 (2u << 8)
#define CR_STRT     (1u << 16)
#define CR_LOCK     (1u << 31)

static void wait_idle(void)
{
    while ((FLASH_SR & SR_BSY) != 0u) {
    }
}

static int finish(void)
{
    wait_idle();
    uint32_t errors = FLASH_SR & SR_ERRORS;
    if (errors != 0u) {
        FLASH_SR = errors; /* error flags are cleared by writing 1 */
        return -1;
    }
    return 0;
}

void flash_unlock(void)
{
    if ((FLASH_CR & CR_LOCK) != 0u) {
        FLASH_KEYR = 0x45670123u;
        FLASH_KEYR = 0xCDEF89ABu;
    }
}

void flash_lock(void)
{
    FLASH_CR |= CR_LOCK;
}

int flash_erase_sector(uint32_t sector)
{
    wait_idle();
    FLASH_SR = SR_ERRORS;
    FLASH_CR = CR_SER | (sector << 3) | CR_PSIZE_32;
    FLASH_CR |= CR_STRT;
    int result = finish();
    FLASH_CR &= ~(CR_SER | CR_SNB_MASK);
    return result;
}

int flash_write_word(uint32_t addr, uint32_t value)
{
    wait_idle();
    FLASH_SR = SR_ERRORS;
    FLASH_CR = CR_PG | CR_PSIZE_32;
    *(volatile uint32_t *)addr = value;
    int result = finish();
    FLASH_CR &= ~CR_PG;
    return result;
}
