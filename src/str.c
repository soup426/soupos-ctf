#include "str.h"
#include <stdint.h>

void *memset(void *dst, int val, size_t n) {
    unsigned char *p = (unsigned char *)dst;
    unsigned char  v = (unsigned char)val;
    while (n--) *p++ = v;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char       *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dst;
}

/* memmove handles overlapping regions by choosing copy direction */
void *memmove(void *dst, const void *src, size_t n) {
    unsigned char       *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d < s || d >= s + n) {
        while (n--) *d++ = *s++;
    } else {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *p = (const unsigned char *)a;
    const unsigned char *q = (const unsigned char *)b;
    while (n--) {
        if (*p != *q) return (int)*p - (int)*q;
        p++; q++;
    }
    return 0;
}

size_t strlen(const char *s) {
    size_t n = 0;
    while (*s++) n++;
    return n;
}

char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++)) {}
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n) {
    char *d = dst;
    while (n && (*d++ = *src++)) n--;
    while (n--) *d++ = '\0';
    return dst;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    while (n && *a && *a == *b) { a++; b++; n--; }
    if (!n) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

char *strcat(char *dst, const char *src) {
    char *d = dst;
    while (*d) d++;
    while ((*d++ = *src++)) {}
    return dst;
}

char *strchr(const char *s, int c) {
    for (; *s; s++)
        if ((unsigned char)*s == (unsigned char)c) return (char *)s;
    if (c == '\0') return (char *)s;
    return (void *)0;
}

/* ── kvformat: the kernel's one printf ────────────────────────────────────
 * Moved out of vga_printf (v0.35.2) so a terminal that is not the VGA console
 * formats exactly the same bytes. Supports what vga_printf always did: %s %c
 * %d %u %x %X %p %%, a '-' flag, a '0' flag and a width. Two quirks kept on
 * purpose because output depends on them: a negative %d prints its '-' BEFORE
 * the padding, and %p is always eight zero-padded hex digits. */
void kvformat(void (*emit)(char c, void *ctx), void *ctx, const char *fmt, va_list args) {
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { emit(*p, ctx); continue; }
        p++;
        if (!*p) { emit('%', ctx); break; }      /* a trailing '%': do not walk past the end */

        int left_align = 0;
        if (*p == '-') { left_align = 1; p++; }
        int zero_pad = 0;
        if (*p == '0') { zero_pad = 1; p++; }

        int width = 0;
        while (*p >= '0' && *p <= '9') { width = width * 10 + (*p - '0'); p++; }

        /* buf[0..len-1] holds digits in reverse order */
        #define EMIT_NUM(buf, len, pad_char) do {           \
            int _l = (len), _w = width;                     \
            char _pc = (pad_char);                          \
            if (!left_align)                                \
                for (int _k = _l; _k < _w; _k++) emit(_pc, ctx); \
            for (int _j = _l - 1; _j >= 0; _j--) emit((buf)[_j], ctx); \
            if (left_align)                                 \
                for (int _k = _l; _k < _w; _k++) emit(' ', ctx); \
        } while (0)

        switch (*p) {
            case 's': {
                const char *s = va_arg(args, const char *);
                if (!s) s = "(null)";
                int slen = 0;
                while (s[slen]) slen++;
                if (!left_align)
                    for (int k = slen; k < width; k++) emit(' ', ctx);
                for (int k = 0; k < slen; k++) emit(s[k], ctx);
                if (left_align)
                    for (int k = slen; k < width; k++) emit(' ', ctx);
                break;
            }
            case 'c':
                emit((char)va_arg(args, int), ctx);
                break;
            case 'd': {
                int val = va_arg(args, int);
                int neg = (val < 0);
                unsigned uval = neg ? (unsigned)(-(val + 1)) + 1u : (unsigned)val;
                char buf[12]; int len = 0;
                if (uval == 0) { buf[len++] = '0'; }
                else { while (uval) { buf[len++] = (char)('0' + (uval % 10)); uval /= 10; } }
                if (neg) emit('-', ctx);
                EMIT_NUM(buf, len, zero_pad ? '0' : ' ');
                break;
            }
            case 'u': {
                unsigned val = va_arg(args, unsigned);
                char buf[12]; int len = 0;
                if (val == 0) { buf[len++] = '0'; }
                else { while (val) { buf[len++] = (char)('0' + (val % 10)); val /= 10; } }
                EMIT_NUM(buf, len, zero_pad ? '0' : ' ');
                break;
            }
            case 'x': case 'X': case 'p': {
                unsigned val;
                if (*p == 'p') {
                    val = (unsigned)(uintptr_t)va_arg(args, void *);
                    zero_pad = 1; width = 8;
                } else {
                    val = va_arg(args, unsigned);
                }
                const char *hex = (*p == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
                char buf[9]; int len = 0;
                if (val == 0) { buf[len++] = '0'; }
                else { while (val) { buf[len++] = hex[val & 0xF]; val >>= 4; } }
                EMIT_NUM(buf, len, zero_pad ? '0' : ' ');
                break;
            }
            case '%':
                emit('%', ctx);
                break;
            default:
                emit('%', ctx);
                emit(*p, ctx);
                break;
        }
        #undef EMIT_NUM
    }
}
