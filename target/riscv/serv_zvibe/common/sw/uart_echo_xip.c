/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * UART Echo - XIP Version (Execute-In-Place from Flash)
 *
 * Tests:
 * - XIP fetch (CPU executing from flash)
 * - UART TX (echo and banner)
 * - UART RX (receive characters)
 *
 * Board-agnostic: link origin set per-platform via --defsym=__flash_origin
 */

// UART Wishbone registers
#define UART_BASE     0x40000000
#define UART_TX_DATA  (*(volatile unsigned int*)(UART_BASE + 0x00))
#define UART_RX_DATA  (*(volatile unsigned int*)(UART_BASE + 0x04))
#define UART_STATUS   (*(volatile unsigned int*)(UART_BASE + 0x08))

#define TX_READY  (1 << 0)
#define RX_READY  (1 << 1)

// Blocking write - poll TX_READY
static void uart_putc(char c) {
    while (!(UART_STATUS & TX_READY));
    UART_TX_DATA = c;
}

static void uart_puts(const char *s) {
    while (*s) {
        uart_putc(*s++);
    }
}

int main(void) {
    char line_buffer[80];
    int pos = 0;

    // Print banner
    uart_puts("\r\n==============================\r\n");
    uart_puts("UART Echo Test (XIP)\r\n");
    uart_puts("==============================\r\n");
    uart_puts("Ready> ");

    // Line-buffered echo loop
    while (1) {
        if (UART_STATUS & RX_READY) {
            char c = (char)(UART_RX_DATA & 0xFF);

            if (c == '\r' || c == '\n') {
                uart_puts("\r\n");
                if (pos > 0) {
                    uart_puts("You typed: ");
                    line_buffer[pos] = '\0';
                    uart_puts(line_buffer);
                    uart_puts("\r\n");
                    pos = 0;
                }
                uart_puts("Ready> ");
            }
            else if (c == 0x08 || c == 0x7F) {
                // Backspace / DEL
                if (pos > 0) {
                    pos--;
                    uart_putc(0x08);
                    uart_putc(' ');
                    uart_putc(0x08);
                }
            }
            else {
                // Echo character immediately, buffer for replay on Enter
                uart_putc(c);
                if (pos < (int)sizeof(line_buffer) - 1) {
                    line_buffer[pos++] = c;
                }
            }
        }
    }

    return 0;
}
