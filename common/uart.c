#include <stdint.h>
#include "uart.h"

#define RCC_AHB1ENR (*(volatile uint32_t *)0x40023830u)
#define RCC_APB1ENR (*(volatile uint32_t *)0x40023840u)
#define GPIOA_MODER (*(volatile uint32_t *)0x40020000u)
#define GPIOA_AFRL  (*(volatile uint32_t *)0x40020020u)
#define USART2_SR   (*(volatile uint32_t *)0x40004400u)
#define USART2_DR   (*(volatile uint32_t *)0x40004404u)
#define USART2_BRR  (*(volatile uint32_t *)0x40004408u)
#define USART2_CR1  (*(volatile uint32_t *)0x4000440Cu)

void uart_init(void)
{
    RCC_AHB1ENR |= (1u << 0);  /* GPIOA clock */
    RCC_APB1ENR |= (1u << 17); /* USART2 clock */
    (void)RCC_APB1ENR;

    /* PA2 (TX) and PA3 (RX): alternate function 7 = USART2 */
    GPIOA_MODER &= ~((3u << 4) | (3u << 6));
    GPIOA_MODER |= (2u << 4) | (2u << 6);
    GPIOA_AFRL &= ~((0xFu << 8) | (0xFu << 12));
    GPIOA_AFRL |= (7u << 8) | (7u << 12);

    USART2_CR1 = 0;
    USART2_BRR = 139u; /* 16 MHz / 115200 baud */
    USART2_CR1 = (1u << 13) | (1u << 3) | (1u << 2); /* enable, TX, RX */
}

void uart_putc(char c)
{
    while ((USART2_SR & (1u << 7)) == 0u) {
    }
    USART2_DR = (uint8_t)c;
}

void uart_puts(const char *s)
{
    while (*s != '\0') {
        uart_putc(*s++);
    }
    /* wait until the last byte has fully left the pin */
    while ((USART2_SR & (1u << 6)) == 0u) {
    }
}
