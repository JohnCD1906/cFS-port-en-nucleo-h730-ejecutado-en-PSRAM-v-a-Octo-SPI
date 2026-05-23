/* ============================================================
 * uart_debug.h
 * Debug UART helper — UART8, 115200 8N1, HSI@64MHz
 * Pin swap habilitado (TX→PE1, RX→PE0 o según config HAL)
 * ============================================================ */

#ifndef UART_DEBUG_H
#define UART_DEBUG_H

#include <stdint.h>
#include <stdarg.h>

void uart_init(void);
void uart_putchar(char c);
void uart_puts(const char *s);
void uart_printf(const char *fmt, ...);

#endif /* UART_DEBUG_H */
