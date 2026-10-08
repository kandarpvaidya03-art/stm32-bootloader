#include <stdint.h>
#include "flash.h"
#include "uart.h"
#include "selftest.h"

#define RCC_AHB1ENR (*(volatile uint32_t *)0x40023830u)
#define GPIOC_IDR   (*(volatile uint32_t *)0x40020810u)

#define SLOT_B_BASE   0x08040000u
#define SLOT_B_SECTOR 6u
#define TEST_WORDS    16u

static int run(void)
{
    const volatile uint32_t *slot = (const volatile uint32_t *)SLOT_B_BASE;

    uart_puts("selftest: erasing slot B\r\n");
    if (flash_erase_sector(SLOT_B_SECTOR) != 0) {
        uart_puts("selftest: FAIL erase reported an error\r\n");
        return -1;
    }
    for (uint32_t i = 0; i < TEST_WORDS; i++) {
        if (slot[i] != 0xFFFFFFFFu) {
            uart_puts("selftest: FAIL slot not blank after erase\r\n");
            return -1;
        }
    }

    uart_puts("selftest: writing pattern\r\n");
    for (uint32_t i = 0; i < TEST_WORDS; i++) {
        if (flash_write_word(SLOT_B_BASE + 4u * i, 0xA5A50000u + i) != 0) {
            uart_puts("selftest: FAIL write reported an error\r\n");
            return -1;
        }
    }
    for (uint32_t i = 0; i < TEST_WORDS; i++) {
        if (slot[i] != 0xA5A50000u + i) {
            uart_puts("selftest: FAIL read-back mismatch\r\n");
            return -1;
        }
    }
    return 0;
}

void selftest_if_button_held(void)
{
    RCC_AHB1ENR |= (1u << 2); /* GPIOC clock; button B1 is on PC13 */
    (void)RCC_AHB1ENR;
    if ((GPIOC_IDR & (1u << 13)) != 0u) {
        return; /* the pin reads low while the button is pressed */
    }

    flash_unlock();
    if (run() == 0) {
        uart_puts("selftest: PASS\r\n");
    }
    flash_lock();
}
