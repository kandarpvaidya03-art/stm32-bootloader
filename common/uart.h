#ifndef UART_H
#define UART_H

#include <stdint.h>

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);
int uart_read_byte(uint8_t *out); /* 1 if a byte was received, 0 if none waiting */

#endif
