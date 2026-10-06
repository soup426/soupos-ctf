/* ulib.h - tiny user-space runtime for soupOS ring-3 programs.
 *
 * No libc. A program just defines `int main(void)`; ulib provides _start
 * (which calls main and then exits with its return value) plus thin wrappers
 * over the int 0x80 syscalls and a few conveniences. Link ulib.c alongside
 * the program (see the Makefile) at USER_BASE via user/user.ld.
 */
#pragma once
#include "../src/syscall_nr.h"

/* raw syscalls */
int  sys_write(int fd, const char *buf, int len);
int  sys_read (int fd, char *buf, int max);
int  sys_open (const char *path, int mode);
int  sys_close(int fd);
void *sys_sbrk(int incr);
int  sys_args (char *buf, int max);
void sys_yield(void);
unsigned sys_ticks(void);            /* uptime in 100 Hz PIT ticks */
__attribute__((noreturn)) void sys_exit(int code);

/* conveniences */
int   strlen_(const char *s);
void  print(const char *s);          /* -> screen (fd 1)  */
void  eprint(const char *s);         /* -> serial (fd 2)  */
void  print_int(int v);              /* decimal, to screen */
void  eprint_int(int v);             /* decimal, to serial */
void *malloc(unsigned n);            /* bump allocator over sbrk */
