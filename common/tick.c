#include <stdint.h>
#include "tick.h"

#define SYST_CSR (*(volatile uint32_t *)0xE000E010u)
#define SYST_RVR (*(volatile uint32_t *)0xE000E014u)
#define SYST_CVR (*(volatile uint32_t *)0xE000E018u)

void tick_init(void)
{
    SYST_CSR = 0;
    SYST_RVR = 16000u - 1u; /* 16 MHz / 16000 = 1 kHz */
    SYST_CVR = 0;
    SYST_CSR = 5u;          /* enable, CPU clock, no interrupt */
}

void tick_stop(void)
{
    SYST_CSR = 0;
}

int tick_elapsed(void)
{
    return (SYST_CSR & (1u << 16)) != 0u; /* flag clears when read */
}
