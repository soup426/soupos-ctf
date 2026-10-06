#pragma once
#include <stdint.h>

/* klog.h - kernel log ring buffer.
 *
 * Every klog write lands in a fixed in-memory ring AND is mirrored to
 * COM1 so a serial capture and the in-RAM log always agree. The shell's
 * `dmesg` command dumps the ring; the panic handler appends to it so a
 * crash leaves a trace even when the screen is lost.
 *
 * No heap - the ring is static BSS, so klog is usable from the very
 * first boot step and from interrupt context. */

#define KLOG_SIZE 8192

/* Optional: reset the ring. BSS is already zeroed, so calling this is
 * only needed to discard earlier output. */
void     klog_init(void);

void     klog_putc(char c);
void     klog_puts(const char *s);

/* printf subset: %s %u %d %x %p %c %%. */
void     klog(const char *fmt, ...);

/* Copy the log (oldest->newest) into out, up to max bytes. If the ring
 * holds more than max, the oldest bytes are dropped. Returns byte count. */
uint32_t klog_copy(char *out, uint32_t max);

/* Bytes currently held in the ring (saturates at KLOG_SIZE). */
uint32_t klog_len(void);
