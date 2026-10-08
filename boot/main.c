#include <stdint.h>
#include "uart.h"

#define RCC_AHB1ENR (*(volatile uint32_t *)0x40023830u)
#define GPIOA_MODER (*(volatile uint32_t *)0x40020000u)
#define GPIOA_ODR   (*(volatile uint32_t *)0x40020014u)
#define SCB_VTOR    (*(volatile uint32_t *)0xE000ED08u)

#define APP_BASE  0x08020000u
#define APP_END   0x08040000u
#define RAM_START 0x20000000u
#define RAM_END   0x20018000u

static void delay(uint32_t n)
{
    for (volatile uint32_t i = 0; i < n; i++) {
    }
}

static void blink_fast(uint32_t times)
{
    for (uint32_t i = 0; i < times * 2u; i++) {
        GPIOA_ODR ^= (1u << 5);
        delay(100000u);
    }
}

static int app_looks_valid(void)
{
    const uint32_t *vt = (const uint32_t *)APP_BASE;
    uint32_t sp = vt[0];
    uint32_t pc = vt[1];

    if (sp <= RAM_START || sp > RAM_END) {
        return 0; /* stack pointer not in RAM (erased flash reads 0xFFFFFFFF) */
    }
    if (pc < APP_BASE || pc >= APP_END) {
        return 0; /* reset address not inside the slot */
    }
    if ((pc & 1u) == 0u) {
        return 0; /* Cortex-M code addresses always have bit 0 set */
    }
    return 1;
}

__attribute__((noreturn)) static void jump_to_app(void)
{
    const uint32_t *vt = (const uint32_t *)APP_BASE;
    uint32_t sp = vt[0];
    uint32_t pc = vt[1];

    SCB_VTOR = APP_BASE;
    __asm volatile("msr msp, %0\n"
                   "bx %1\n"
                   :
                   : "r"(sp), "r"(pc));
    __builtin_unreachable();
}

int main(void)
{
    RCC_AHB1ENR |= (1u << 0);
    (void)RCC_AHB1ENR;
    GPIOA_MODER &= ~(3u << 10);
    GPIOA_MODER |= (1u << 10);

    uart_init();
    uart_puts("boot: started\r\n");
    blink_fast(5);

    if (app_looks_valid()) {
        uart_puts("boot: jumping to slot A\r\n");
        jump_to_app();
    }

    uart_puts("boot: no valid app, staying in bootloader\r\n");
    for (;;) {
        blink_fast(1);
    }
}
