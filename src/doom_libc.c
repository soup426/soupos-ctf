/* doom_libc.c - C standard library shim for soupOS / Doom port
 *
 * Provides the hosted-C functions that Doom expects, wired to kernel
 * facilities (heap, FAT file handles, VGA console, timer).
 *
 * Include doom_libc.h (or the individual stdio.h / stdlib.h / etc.)
 * in Doom source files instead of the real standard headers.
 */

#include "doom_libc.h"
#include "heap.h"
#include "fat.h"
#include "vga.h"
#include "timer.h"
#include "str.h"
#include <stdarg.h>
#include <stdint.h>

/* ── errno ─────────────────────────────────────────────────────────────── */
int errno = 0;

/* ── kvsnprintf - format-string engine ─────────────────────────────────── */
/*
 * Writes at most `size` bytes (including the terminating '\0') to buf.
 * Returns the number of characters that would have been written (not
 * counting '\0') had size been large enough - i.e. the POSIX snprintf
 * return convention.
 *
 * Supported:  %s %c %d %i %u %x %X %p %% %ld %lu %lx %lX
 *             flags: - (left-align), 0 (zero-pad), + (force sign)
 *             width (decimal), precision (.N for strings)
 */
int kvsnprintf(char *buf, uint32_t size, const char *fmt, va_list ap) {
    uint32_t out = 0;   /* bytes written (not counting '\0') */

#define PUT(c) do {                             \
    if (out + 1 < size) buf[out] = (char)(c);  \
    out++;                                       \
} while (0)

    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { PUT(*p); continue; }
        p++;

        /* flags */
        int left  = 0, force_sign = 0, zero_pad = 0;
        for (;;) {
            if      (*p == '-') { left  = 1; p++; }
            else if (*p == '+') { force_sign = 1; p++; }
            else if (*p == '0') { zero_pad = 1; p++; }
            else break;
        }
        if (left) zero_pad = 0;   /* left-align overrides zero-pad */

        /* width */
        int width = 0;
        while (*p >= '0' && *p <= '9') { width = width*10 + (*p-'0'); p++; }

        /* precision */
        int has_prec = 0, prec = 0;
        if (*p == '.') {
            p++; has_prec = 1;
            while (*p >= '0' && *p <= '9') { prec = prec*10 + (*p-'0'); p++; }
        }

        /* length modifier */
        int lng = 0;
        if (*p == 'l') { lng = 1; p++; if (*p == 'l') { p++; } }
        if (*p == 'h') p++;   /* ignore short */

        char tmp[24];
        int  tlen = 0;

        switch (*p) {

            /* ── string ─────────────────────────────────────── */
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (!s) s = "(null)";
                int slen = 0;
                while (s[slen]) slen++;
                if (has_prec && slen > prec) slen = prec;
                int pad = (slen < width) ? width - slen : 0;
                if (!left) for (int k = 0; k < pad; k++) PUT(' ');
                for (int k = 0; k < slen; k++) PUT(s[k]);
                if (left)  for (int k = 0; k < pad; k++) PUT(' ');
                break;
            }

            /* ── character ──────────────────────────────────── */
            case 'c': PUT((char)va_arg(ap, int)); break;

            /* ── signed decimal ─────────────────────────────── */
            case 'd': case 'i': {
                long val = lng ? va_arg(ap, long) : (long)va_arg(ap, int);
                int neg  = (val < 0);
                unsigned long uv = neg ? (unsigned long)(-(val+1)) + 1UL
                                       : (unsigned long)val;
                if (!uv) tmp[tlen++] = '0';
                else { while (uv) { tmp[tlen++] = '0' + (int)(uv%10); uv /= 10; } }
                /* tmp holds digits in reverse; tlen = digit count */
                int sign_ch = neg ? '-' : (force_sign ? '+' : 0);
                int total   = tlen + (sign_ch ? 1 : 0);
                int pad     = (total < width) ? width - total : 0;
                if (!left && !zero_pad) for (int k = 0; k < pad; k++) PUT(' ');
                if (sign_ch) PUT(sign_ch);
                if (!left &&  zero_pad) for (int k = 0; k < pad; k++) PUT('0');
                for (int j = tlen-1; j >= 0; j--) PUT(tmp[j]);
                if (left) for (int k = 0; k < pad; k++) PUT(' ');
                break;
            }

            /* ── unsigned decimal ───────────────────────────── */
            case 'u': {
                unsigned long val = lng ? va_arg(ap, unsigned long)
                                        : (unsigned long)va_arg(ap, unsigned);
                if (!val) tmp[tlen++] = '0';
                else { while (val) { tmp[tlen++] = '0' + (int)(val%10); val /= 10; } }
                int pad = (tlen < width) ? width - tlen : 0;
                char pc = zero_pad ? '0' : ' ';
                if (!left) for (int k = 0; k < pad; k++) PUT(pc);
                for (int j = tlen-1; j >= 0; j--) PUT(tmp[j]);
                if (left)  for (int k = 0; k < pad; k++) PUT(' ');
                break;
            }

            /* ── hex / pointer ──────────────────────────────── */
            case 'x': case 'X': case 'p': {
                unsigned long val;
                if (*p == 'p')
                    val = (unsigned long)(uintptr_t)va_arg(ap, void *);
                else
                    val = lng ? va_arg(ap, unsigned long)
                              : (unsigned long)va_arg(ap, unsigned);
                const char *hex = (*p == 'X') ? "0123456789ABCDEF"
                                               : "0123456789abcdef";
                if (!val) tmp[tlen++] = '0';
                else { while (val) { tmp[tlen++] = hex[val & 0xF]; val >>= 4; } }
                int pad = (tlen < width) ? width - tlen : 0;
                char pc = zero_pad ? '0' : ' ';
                if (!left) for (int k = 0; k < pad; k++) PUT(pc);
                for (int j = tlen-1; j >= 0; j--) PUT(tmp[j]);
                if (left)  for (int k = 0; k < pad; k++) PUT(' ');
                break;
            }

            /* ── octal ──────────────────────────────────────── */
            case 'o': {
                unsigned long val = lng ? va_arg(ap, unsigned long)
                                        : (unsigned long)va_arg(ap, unsigned);
                if (!val) tmp[tlen++] = '0';
                else { while (val) { tmp[tlen++] = '0' + (int)(val & 7); val >>= 3; } }
                int pad = (tlen < width) ? width - tlen : 0;
                if (!left) for (int k = 0; k < pad; k++) PUT(zero_pad ? '0' : ' ');
                for (int j = tlen-1; j >= 0; j--) PUT(tmp[j]);
                if (left)  for (int k = 0; k < pad; k++) PUT(' ');
                break;
            }

            case '%': PUT('%'); break;
            default:  PUT('%'); PUT(*p); break;
        }
    }

    if (size > 0) buf[out < size ? out : size-1] = '\0';
    return (int)out;

#undef PUT
}

int kvsprintf(char *buf, const char *fmt, va_list ap) {
    /* Unbounded - caller must ensure buf is large enough */
    return kvsnprintf(buf, 0x7FFFFFFFu, fmt, ap);
}

int ksprintf(char *buf, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n = kvsprintf(buf, fmt, ap);
    va_end(ap); return n;
}

int ksnprintf(char *buf, uint32_t size, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n = kvsnprintf(buf, size, fmt, ap);
    va_end(ap); return n;
}

/* ── FILE abstraction ──────────────────────────────────────────────────── */

/* Sentinel fd values for the virtual streams */
#define FD_STDOUT  -1
#define FD_STDERR  -2
#define FD_STDIN   -3

static doom_FILE _stdout_f = { FD_STDOUT, 0 };
static doom_FILE _stderr_f = { FD_STDERR, 0 };
static doom_FILE _stdin_f  = { FD_STDIN,  0 };

doom_FILE *doom_stdout = &_stdout_f;
doom_FILE *doom_stderr = &_stderr_f;
doom_FILE *doom_stdin  = &_stdin_f;

doom_FILE *doom_fopen(const char *path, const char *mode) {
    (void)mode;   /* only read-only for now */
    int fd = fat_fopen(path);
    if (fd < 0) return (doom_FILE *)0;
    doom_FILE *fp = (doom_FILE *)kmalloc(sizeof(doom_FILE));
    if (!fp) { fat_fclose(fd); return (doom_FILE *)0; }
    fp->fd  = fd;
    fp->eof = 0;
    return fp;
}

int doom_fclose(doom_FILE *fp) {
    if (!fp || fp->fd < 0) return 0;
    fat_fclose(fp->fd);
    kfree(fp);
    return 0;
}

uint32_t doom_fread(void *buf, uint32_t size, uint32_t count, doom_FILE *fp) {
    if (!fp || fp->fd < 0) return 0;
    uint32_t total = size * count;
    if (total == 0) return 0;
    int got = fat_fread(fp->fd, buf, total);
    if (got <= 0) { fp->eof = 1; return 0; }
    if ((uint32_t)got < total) fp->eof = 1;
    return (uint32_t)got / size;
}

int doom_fseek(doom_FILE *fp, long offset, int whence) {
    if (!fp || fp->fd < 0) return -1;
    fp->eof = 0;
    return fat_fseek(fp->fd, (int32_t)offset,
                     whence == 0 ? FAT_SEEK_SET :
                     whence == 1 ? FAT_SEEK_CUR : FAT_SEEK_END);
}

long doom_ftell(doom_FILE *fp) {
    if (!fp || fp->fd < 0) return -1;
    return (long)fat_ftell(fp->fd);
}

int doom_feof(doom_FILE *fp) {
    if (!fp) return 1;
    if (fp->fd < 0) return 1;
    return fat_feof(fp->fd);
}

int doom_fgetc(doom_FILE *fp) {
    unsigned char c;
    if (doom_fread(&c, 1, 1, fp) != 1) return -1; /* EOF */
    return (int)c;
}

int doom_fprintf(doom_FILE *fp, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    char buf[512];
    int n = kvsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (fp->fd == FD_STDOUT || fp->fd == FD_STDERR)
        vga_puts(buf);
    return n;
}

int doom_printf(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    char buf[512];
    int n = kvsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    vga_puts(buf);
    return n;
}

int doom_vprintf(const char *fmt, va_list ap) {
    char buf[512];
    int n = kvsnprintf(buf, sizeof(buf), fmt, ap);
    vga_puts(buf);
    return n;
}

int doom_vfprintf(doom_FILE *fp, const char *fmt, va_list ap) {
    char buf[512];
    int n = kvsnprintf(buf, sizeof(buf), fmt, ap);
    if (fp->fd == FD_STDOUT || fp->fd == FD_STDERR)
        vga_puts(buf);
    return n;
}

int doom_puts(const char *s) {
    vga_puts(s); vga_putchar('\n'); return 0;
}

int doom_fputs(const char *s, doom_FILE *fp) {
    if (fp->fd == FD_STDOUT || fp->fd == FD_STDERR) vga_puts(s);
    return 0;
}

/* ── memory ────────────────────────────────────────────────────────────── */
void *doom_malloc (uint32_t size)              { return kmalloc(size);  }
void  doom_free   (void *p)                    { kfree(p);              }
void *doom_realloc(void *p, uint32_t size)     { return krealloc(p, size); }
void *doom_calloc (uint32_t n, uint32_t size)  { return kcalloc(n, size);  }

/* ── stdlib helpers ────────────────────────────────────────────────────── */
void doom_exit(int code) {
    (void)code;
    vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
    vga_puts("\n[DOOM] exit() called - system halted.\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    __asm__ volatile ("cli; hlt");
    for (;;) {}
}

void doom_abort(void) { doom_exit(1); }

int doom_atoi(const char *s) {
    int n = 0, neg = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') n = n*10 + (*s++ - '0');
    return neg ? -n : n;
}

long doom_atol(const char *s) { return (long)doom_atoi(s); }

long doom_strtol(const char *s, char **end, int base) {
    while (*s == ' ') s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    if (base == 0) {
        if (s[0]=='0' && (s[1]=='x'||s[1]=='X')) { base=16; s+=2; }
        else if (s[0]=='0') { base=8; s++; }
        else base=10;
    } else if (base==16 && s[0]=='0' && (s[1]=='x'||s[1]=='X')) s+=2;
    long n = 0;
    for (;;) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        n = n*base + d; s++;
    }
    if (end) *end = (char *)s;
    return neg ? -n : n;
}

unsigned long doom_strtoul(const char *s, char **end, int base) {
    return (unsigned long)doom_strtol(s, end, base);
}

/* Simple LCG random (good enough for Doom which uses its own tables anyway) */
static unsigned long rand_state = 12345;
int doom_rand(void)           { rand_state = rand_state * 1103515245UL + 12345; return (int)((rand_state >> 16) & 0x7FFF); }
void doom_srand(unsigned s)   { rand_state = s; }

/* NULL-returning getenv */
char *doom_getenv(const char *n) { (void)n; return (char *)0; }

/* system() - always fails */
int doom_system(const char *cmd) { (void)cmd; return -1; }

/* ── time ──────────────────────────────────────────────────────────────── */
long doom_time(long *t) {
    long s = (long)timer_get_seconds();
    if (t) *t = s;
    return s;
}

/* ── integer math ──────────────────────────────────────────────────────── */
int  doom_abs (int  x) { return x < 0 ? -x : x; }
long doom_labs(long x) { return x < 0 ? -x : x; }

/* Integer square root (Newton's method) */
int doom_isqrt(int x) {
    if (x <= 0) return 0;
    int r = x, n;
    do { n = r; r = (r + x/r) / 2; } while (r < n);
    return n;
}

/* ── ctype ─────────────────────────────────────────────────────────────── */
int doom_isdigit (int c) { return c >= '0' && c <= '9'; }
int doom_isalpha (int c) { return (c>='a'&&c<='z')||(c>='A'&&c<='Z'); }
int doom_isalnum (int c) { return doom_isdigit(c) || doom_isalpha(c); }
int doom_isspace (int c) { return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\f'||c=='\v'; }
int doom_isupper (int c) { return c >= 'A' && c <= 'Z'; }
int doom_islower (int c) { return c >= 'a' && c <= 'z'; }
int doom_isprint (int c) { return c >= 0x20 && c < 0x7F; }
int doom_toupper (int c) { return doom_islower(c) ? c - 32 : c; }
int doom_tolower (int c) { return doom_isupper(c) ? c + 32 : c; }

/* ── extra string functions not in str.h ────────────────────────────────── */
char *doom_strdup(const char *s) {
    if (!s) return (char *)0;
    uint32_t len = (uint32_t)strlen(s) + 1;
    char *p = (char *)kmalloc(len);
    if (p) memcpy(p, s, len);
    return p;
}

char *doom_strupr(char *s) {
    for (char *p = s; *p; p++) *p = (char)doom_toupper(*p);
    return s;
}
char *doom_strlwr(char *s) {
    for (char *p = s; *p; p++) *p = (char)doom_tolower(*p);
    return s;
}

/* strrchr - find last occurrence of c in s */
char *doom_strrchr(const char *s, int c) {
    const char *last = (char *)0;
    for (; *s; s++) if (*s == (char)c) last = s;
    return (char *)last;
}

/* strncat */
char *doom_strncat(char *dst, const char *src, uint32_t n) {
    char *d = dst;
    while (*d) d++;
    while (n-- && *src) *d++ = *src++;
    *d = '\0';
    return dst;
}
