/* klog.c - kernel log ring buffer. See klog.h for the design. */

#include "klog.h"
#include "serial.h"
#include <stdarg.h>

static char     ring[KLOG_SIZE];
static uint32_t head;     /* next write index                          */
static uint32_t count;    /* bytes held, saturates at KLOG_SIZE         */

void klog_init(void) {
    head  = 0;
    count = 0;
}

void klog_putc(char c) {
    ring[head] = c;
    head = (head + 1) % KLOG_SIZE;
    if (count < KLOG_SIZE) count++;
    serial_putc(c);                 /* mirror every byte to COM1 */
}

void klog_puts(const char *s) {
    while (*s) klog_putc(*s++);
}

/* ---- minimal formatting helpers ---- */
static void emit_uint(uint32_t v) {
    char buf[12];
    int  i = 0;
    if (v == 0) { klog_putc('0'); return; }
    while (v) { buf[i++] = (char)('0' + v % 10); v /= 10; }
    while (i) klog_putc(buf[--i]);
}

static void emit_int(int32_t v) {
    if (v < 0) { klog_putc('-'); emit_uint((uint32_t)(-(int64_t)v)); }
    else       { emit_uint((uint32_t)v); }
}

static void emit_hex(uint32_t v) {
    static const char d[] = "0123456789abcdef";
    char buf[8];
    int  i = 0;
    if (v == 0) { klog_putc('0'); return; }
    while (v) { buf[i++] = d[v & 0xF]; v >>= 4; }
    while (i) klog_putc(buf[--i]);
}

void klog(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    for (; *fmt; fmt++) {
        if (*fmt != '%') { klog_putc(*fmt); continue; }
        fmt++;
        switch (*fmt) {
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (!s) s = "(null)";
                while (*s) klog_putc(*s++);
                break;
            }
            case 'u': emit_uint(va_arg(ap, uint32_t)); break;
            case 'd': emit_int (va_arg(ap, int32_t));  break;
            case 'x': emit_hex (va_arg(ap, uint32_t)); break;
            case 'p':
                klog_putc('0'); klog_putc('x');
                emit_hex(va_arg(ap, uint32_t));
                break;
            case 'c': klog_putc((char)va_arg(ap, int)); break;
            case '%': klog_putc('%'); break;
            case '\0': va_end(ap); return;     /* trailing '%' */
            default:  klog_putc('%'); klog_putc(*fmt); break;
        }
    }
    va_end(ap);
}

uint32_t klog_len(void) {
    return count;
}

uint32_t klog_copy(char *out, uint32_t max) {
    if (!out || max == 0) return 0;
    uint32_t n = (count < max) ? count : max;
    /* Oldest byte: index 0 until the ring wraps, then `head`. */
    uint32_t oldest = (count < KLOG_SIZE) ? 0 : head;
    /* If truncating, drop the oldest (count - n) bytes. */
    uint32_t start  = (oldest + (count - n)) % KLOG_SIZE;
    for (uint32_t i = 0; i < n; i++)
        out[i] = ring[(start + i) % KLOG_SIZE];
    return n;
}
