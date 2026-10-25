#include <stdint.h>
#include "iwdg.h"

#define IWDG_KR  (*(volatile uint32_t *)0x40003000u)
#define IWDG_PR  (*(volatile uint32_t *)0x40003004u)
#define IWDG_RLR (*(volatile uint32_t *)0x40003008u)
#define IWDG_SR  (*(volatile uint32_t *)0x4000300Cu)
#define RCC_CSR  (*(volatile uint32_t *)0x40023874u)
#define DBGMCU_APB1_FZ (*(volatile uint32_t *)0xE0042008u)

#define KEY_START  0xCCCCu
#define KEY_UNLOCK 0x5555u
#define KEY_RELOAD 0xAAAAu

#define PR_DIV64      4u    /* 32 kHz / 64 = 500 Hz */
#define RELOAD_4S     2000u /* 2000 / 500 Hz = 4 s at the nominal LSI frequency */
#define SR_BUSY       0x3u  /* PVU | RVU: prescaler or reload update in progress */
#define CSR_IWDGRSTF  (1u << 29)
#define CSR_RMVF      (1u << 24)
#define DBG_IWDG_STOP (1u << 12)

void iwdg_start(void)
{
    DBGMCU_APB1_FZ |= DBG_IWDG_STOP; /* pause the watchdog while a debugger halts the core */
    IWDG_KR = KEY_START;  /* also starts the LSI */
    IWDG_KR = KEY_UNLOCK; /* allow writes to PR and RLR */
    IWDG_PR = PR_DIV64;
    IWDG_RLR = RELOAD_4S;
    while ((IWDG_SR & SR_BUSY) != 0u) {
    }
    IWDG_KR = KEY_RELOAD; /* start counting down from the new reload value */
}

void iwdg_kick(void)
{
    IWDG_KR = KEY_RELOAD;
}

int reset_was_watchdog(void)
{
    int watchdog = (RCC_CSR & CSR_IWDGRSTF) != 0u;
    RCC_CSR |= CSR_RMVF; /* clear all reset flags for next time */
    return watchdog;
}
