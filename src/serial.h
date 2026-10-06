#pragma once

/* serial.h - COM1 (16550 UART) driver for boot diagnostics and the
 * kernel log mirror. Polled, transmit-focused; 38400 8N1. */

void serial_init(void);
void serial_putc(char c);
void serial_puts(const char *s);

/* COM1 receive, non-blocking: byte 0..255, or -1 if nothing is ready.
 * Used by the serial console (console.c) so soupOS can be driven over a
 * plain socket with no PS/2 keyboard attached. */
int  serial_getc_nb(void);

/* COM2 - bidirectional, polled. Used by the `ai` serial bridge. serial2_init
 * returns 0 if a UART is present at 0x2F8, -1 otherwise; the other calls are
 * no-ops / return -1 when absent. */
int  serial2_init(void);
int  serial2_ok(void);
void serial2_putc(char c);
void serial2_puts(const char *s);
int  serial2_getc_nb(void);   /* byte 0..255, or -1 if none ready */
