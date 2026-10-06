/* ulib.c - user-space runtime. See ulib.h. */
#include "ulib.h"

int sys_write(int fd, const char *buf, int len) {
    int ret;
    __asm__ volatile ("int $0x80"
                      : "=a"(ret)
                      : "a"(SYS_WRITE), "b"(fd), "c"(buf), "d"(len) : "memory");
    return ret;
}

int sys_read(int fd, char *buf, int max) {
    int ret;
    __asm__ volatile ("int $0x80"
                      : "=a"(ret)
                      : "a"(SYS_READ), "b"(fd), "c"(buf), "d"(max) : "memory");
    return ret;
}

int sys_open(const char *path, int mode) {
    int ret;
    __asm__ volatile ("int $0x80"
                      : "=a"(ret) : "a"(SYS_OPEN), "b"(path), "c"(mode) : "memory");
    return ret;
}

int sys_close(int fd) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(SYS_CLOSE), "b"(fd));
    return ret;
}

void *sys_sbrk(int incr) {
    void *ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(SYS_SBRK), "b"(incr) : "memory");
    return ret;
}

int sys_args(char *buf, int max) {
    int ret;
    __asm__ volatile ("int $0x80"
                      : "=a"(ret) : "a"(SYS_ARGS), "b"(buf), "c"(max) : "memory");
    return ret;
}

void sys_yield(void) {
    __asm__ volatile ("int $0x80" : : "a"(SYS_YIELD));
}

unsigned sys_ticks(void) {
    unsigned ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(SYS_TICKS));
    return ret;
}

__attribute__((noreturn)) void sys_exit(int code) {
    __asm__ volatile ("int $0x80" : : "a"(SYS_EXIT), "b"(code));
    for (;;) {}
    __builtin_unreachable();
}

int strlen_(const char *s) { int n = 0; while (s[n]) n++; return n; }

void print(const char *s)  { sys_write(FD_STDOUT, s, strlen_(s)); }
void eprint(const char *s) { sys_write(FD_STDERR, s, strlen_(s)); }

static void fd_int(int fd, int v) {
    char b[12]; int i = 0, neg = v < 0;
    unsigned u = neg ? (unsigned)(-v) : (unsigned)v;
    if (!u) b[i++] = '0';
    while (u) { b[i++] = (char)('0' + u % 10); u /= 10; }
    char out[13]; int o = 0;
    if (neg) out[o++] = '-';
    while (i) out[o++] = b[--i];
    sys_write(fd, out, o);
}

void print_int(int v)  { fd_int(FD_STDOUT, v); }
void eprint_int(int v) { fd_int(FD_STDERR, v); }

/* Bump allocator backed by sbrk; no free(). Good enough for small programs. */
static char *heap_ptr, *heap_end;

void *malloc(unsigned n) {
    n = (n + 7u) & ~7u;
    if (!heap_ptr) { heap_ptr = (char *)sys_sbrk(0); heap_end = heap_ptr; }
    if (heap_ptr + n > heap_end) {
        unsigned need = (n + 4095u) & ~4095u;
        if (sys_sbrk((int)need) == (void *)-1) return 0;
        heap_end += need;
    }
    void *r = heap_ptr; heap_ptr += n; return r;
}

/* Entry point: the kernel jumps here. Run main, then exit cleanly. */
extern int main(void);
void _start(void) { sys_exit(main()); }
