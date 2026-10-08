#include <stdint.h>
#include "bootstate.h"
#include "uart.h"

#ifndef APP_VERSION
#define APP_VERSION 1
#endif
#ifndef APP_CONFIRM
#define APP_CONFIRM 1
#endif
#ifndef APP_BLINK_DELAY
#define APP_BLINK_DELAY 2000000u
#endif

#define RCC_AHB1ENR (*(volatile uint32_t *)0x40023830u)
#define GPIOA_MODER (*(volatile uint32_t *)0x40020000u)
#define GPIOA_ODR   (*(volatile uint32_t *)0x40020014u)

int main(void)
{
    RCC_AHB1ENR |= (1u << 0);   /* enable GPIOA clock */
    (void)RCC_AHB1ENR;
    GPIOA_MODER &= ~(3u << 10); /* PA5: clear mode bits */
    GPIOA_MODER |= (1u << 10);  /* PA5: general-purpose output */

    uart_init();
    uart_puts("app: version ");
    uart_putc((char)(48 + APP_VERSION));
    uart_puts(" running\r\n");

#if APP_CONFIRM
    boot_flags_t flags;
    bootstate_read(&flags);
    if (flags.requested && flags.trial_started && !flags.confirmed) {
        if (bootstate_set(BOOTSTATE_CONFIRMED) == 0) {
            uart_puts("app: update confirmed\r\n");
        } else {
            uart_puts("app: could not confirm update\r\n");
        }
    }
#else
    uart_puts("app: this build never confirms itself\r\n");
#endif

    for (;;) {
        GPIOA_ODR ^= (1u << 5);
        for (volatile uint32_t i = 0; i < APP_BLINK_DELAY; i++) {
        }
    }
}
