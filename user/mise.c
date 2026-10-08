/* mise.c - exercises the user-mode syscall ABI and reports to the serial
 * port (fd 2) with TESTOUT markers, so the whole thing is verifiable headless.
 * Usage: cook mise.elf <some args> */
#include "ulib.h"

static void emit(const char *tag, const char *val) {
    eprint("TESTOUT "); eprint(tag); eprint("="); eprint(val); eprint("\n");
}

int main(void) {
    /* 1) args round-trip */
    char args[128];
    sys_args(args, sizeof(args));
    emit("args", args);

    /* 2) file open/read - HELLO.TXT ships on the disk */
    int fd = sys_open("HELLO.TXT", O_READ);
    if (fd < 0) { emit("open", "FAIL"); }
    else {
        char buf[64];
        int got = sys_read(fd, buf, 18);   /* "Hello from soupOS!" */
        buf[got > 0 ? got : 0] = '\0';
        emit("read", buf);
        sys_close(fd);
    }

    /* 3) heap via malloc/sbrk - write a pattern, read it back */
    char *p = (char *)malloc(2000);        /* forces a page from sbrk */
    if (!p) { emit("heap", "FAIL"); }
    else {
        for (int i = 0; i < 2000; i++) p[i] = (char)('A' + (i % 26));
        char two[3]; two[0] = p[0]; two[1] = p[1999]; two[2] = '\0';
        emit("heap", two);                 /* expect "A" + (1999%26=23 -> 'X') */
    }

    return 7;
}
