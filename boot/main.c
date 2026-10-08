#include <stdint.h>
#include "bootstate.h"
#include "frame.h"
#include "image.h"
#include "install.h"
#include "layout.h"
#include "selftest.h"
#include "tick.h"
#include "uart.h"
#include "update.h"

#define RCC_AHB1ENR (*(volatile uint32_t *)0x40023830u)
#define GPIOA_MODER (*(volatile uint32_t *)0x40020000u)
#define GPIOA_ODR   (*(volatile uint32_t *)0x40020014u)
#define SCB_VTOR    (*(volatile uint32_t *)0xE000ED08u)
#define SCB_AIRCR   (*(volatile uint32_t *)0xE000ED0Cu)

#define APP_VECTORS (SLOT_A_BASE + IMAGE_HEADER_SIZE)
#define APP_END     (SLOT_A_BASE + SLOT_SIZE)

#define LISTEN_WINDOW_MS 500u
#define PROTOCOL_VERSION 1u

#define MSG_PING 0x01u
#define MSG_ACK  0x81u
#define MSG_NACK 0x82u

#define ERR_UNKNOWN_TYPE 0x10u

static frame_parser_t parser;

void send_frame(uint8_t type, const uint8_t *payload, uint16_t len)
{
    uint8_t out[FRAME_OVERHEAD + 16u];
    size_t n = frame_encode(type, payload, len, out, sizeof out);
    for (size_t i = 0; i < n; i++) {
        uart_putc((char)out[i]);
    }
}

static void handle_frame(void)
{
    if (parser.type == MSG_PING) {
        const uint8_t version = PROTOCOL_VERSION;
        send_frame(MSG_ACK, &version, 1);
    } else if (update_handle_frame(&parser) == 0) {
        const uint8_t code = ERR_UNKNOWN_TYPE;
        send_frame(MSG_NACK, &code, 1);
    }
}

/* Listens until the line has been quiet for window_ms.
 * Every valid frame restarts the window. */
static void listen_for_host(uint32_t window_ms)
{
    uint32_t remaining = window_ms;

    frame_parser_reset(&parser);
    while (remaining != 0u) {
        uint8_t byte;
        if (uart_read_byte(&byte)) {
            frame_result_t result = frame_parser_feed(&parser, byte);
            if (result == FRAME_READY) {
                handle_frame();
                remaining = window_ms;
            } else if (result != FRAME_INCOMPLETE) {
                const uint8_t code = (uint8_t)result;
                send_frame(MSG_NACK, &code, 1);
            }
        }
        if (tick_elapsed()) {
            remaining--;
        }
    }
}

static int update_requested(void)
{
    boot_flags_t flags;
    bootstate_read(&flags);
    return flags.requested && !flags.trial_started;
}

__attribute__((noreturn)) static void system_reset(void)
{
    SCB_AIRCR = 0x05FA0004u;
    for (;;) {
    }
}

static int vectors_look_valid(void)
{
    const uint32_t *vt = (const uint32_t *)APP_VECTORS;
    uint32_t sp = vt[0];
    uint32_t pc = vt[1];

    if (sp <= RAM_START || sp > RAM_END) {
        return 0;
    }
    if (pc < APP_VECTORS || pc >= APP_END) {
        return 0;
    }
    return (pc & 1u) != 0u; /* Cortex-M code addresses always have bit 0 set */
}

__attribute__((noreturn)) static void jump_to_app(void)
{
    const uint32_t *vt = (const uint32_t *)APP_VECTORS;
    uint32_t sp = vt[0];
    uint32_t pc = vt[1];

    SCB_VTOR = APP_VECTORS;
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

    selftest_if_button_held();

    install_run_pending();

    tick_init();
    listen_for_host(LISTEN_WINDOW_MS);
    if (update_requested()) {
        uart_puts("boot: restarting to install\r\n");
        system_reset();
    }

    if (slot_image_ok(SLOT_A_BASE) && vectors_look_valid()) {
        tick_stop(); /* hand the timer back in its reset state */
        uart_puts("boot: jumping to slot A\r\n");
        jump_to_app();
    }

    uart_puts("boot: no valid signed image in slot A, waiting for host\r\n");
    for (;;) {
        listen_for_host(200u);
        GPIOA_ODR ^= (1u << 5);
        if (update_requested()) {
            uart_puts("boot: restarting to install\r\n");
            system_reset();
        }
    }
}
