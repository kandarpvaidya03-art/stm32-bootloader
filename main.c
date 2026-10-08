#include <stdint.h>

#define RCC_AHB1ENR (*(volatile uint32_t *)0x40023830u)
#define GPIOA_MODER (*(volatile uint32_t *)0x40020000u)
#define GPIOA_ODR   (*(volatile uint32_t *)0x40020014u)

int main(void)
{
    RCC_AHB1ENR |= (1u << 0);   /* enable GPIOA clock */
    (void)RCC_AHB1ENR;          /* short delay before using the port */

    GPIOA_MODER &= ~(3u << 10); /* PA5: clear mode bits */
    GPIOA_MODER |= (1u << 10);  /* PA5: general-purpose output */

    for (;;) {
        GPIOA_ODR ^= (1u << 5);
        for (volatile uint32_t i = 0; i < 400000u; i++) {
        }
    }
}
