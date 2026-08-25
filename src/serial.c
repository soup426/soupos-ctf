/* serial.c - polled COM1 UART driver. */

#include "serial.h"
#include <stdint.h>

#define COM1 0x3F8
#define COM2 0x2F8

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t r;
    __asm__ volatile ("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}

void serial_init(void) {
    outb(COM1 + 1, 0x00);   /* disable interrupts            */
    outb(COM1 + 3, 0x80);   /* DLAB on                       */
    outb(COM1 + 0, 0x03);   /* divisor low  - 38400 baud     */
    outb(COM1 + 1, 0x00);   /* divisor high                  */
    outb(COM1 + 3, 0x03);   /* DLAB off, 8 bits, no parity   */
    outb(COM1 + 2, 0xC7);   /* enable + clear FIFO           */
    outb(COM1 + 4, 0x0B);   /* RTS/DSR set                   */
}

void serial_putc(char c) {
    while (!(inb(COM1 + 5) & 0x20)) {}   /* wait for THR empty */
    outb(COM1, (uint8_t)c);
}

void serial_puts(const char *s) {
    while (*s) serial_putc(*s++);
}

/* Non-blocking read of one byte from COM1. Returns -1 when the receiver
 * holding register is empty (LSR bit 0 = data ready). */
int serial_getc_nb(void) {
    if (inb(COM1 + 5) & 0x01) return (int)inb(COM1);
    return -1;
}

/* ── COM2 - the AI bridge channel ────────────────────────────────────────
 * A second polled UART, kept separate from COM1 so the kernel log mirror
 * never collides with AI traffic. Read + write. Presence is probed via the
 * 16550 scratch register so the `ai` command degrades gracefully (rather
 * than spinning forever on THR-empty) when QEMU was started without a COM2
 * backend. */
static int com2_present;

int serial2_init(void) {
    outb(COM2 + 7, 0xAA);                  /* scratch register round-trip */
    if (inb(COM2 + 7) != 0xAA) { com2_present = 0; return -1; }
    outb(COM2 + 1, 0x00);                  /* no interrupts (polled)      */
    outb(COM2 + 3, 0x80);                  /* DLAB on                     */
    outb(COM2 + 0, 0x03);                  /* 38400 baud                  */
    outb(COM2 + 1, 0x00);
    outb(COM2 + 3, 0x03);                  /* 8N1                         */
    outb(COM2 + 2, 0xC7);                  /* enable + clear FIFO         */
    outb(COM2 + 4, 0x0B);                  /* RTS/DSR                     */
    com2_present = 1;
    return 0;
}

int serial2_ok(void) { return com2_present; }

void serial2_putc(char c) {
    if (!com2_present) return;
    int t = 100000;
    while (!(inb(COM2 + 5) & 0x20) && --t) {}   /* bounded THR-empty wait */
    outb(COM2, (uint8_t)c);
}

void serial2_puts(const char *s) {
    while (*s) serial2_putc(*s++);
}

int serial2_getc_nb(void) {
    if (!com2_present) return -1;
    if (inb(COM2 + 5) & 0x01) return (int)inb(COM2);   /* data-ready bit */
    return -1;
}
