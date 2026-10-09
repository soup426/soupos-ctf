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
int  sys_env  (char *buf, int max);   /* NAME=value lines (v0.60.26) */
/* The arguments split as sh splits them (v0.57.2): spaces separate them,
 * except inside '...' or "..." where everything is kept and the quotes
 * are dropped. Fills argv with up to `max` pointers into a static buffer;
 * returns how many. */
int  uargv(char **argv, int max);
void sys_yield(void);
unsigned sys_ticks(void);            /* uptime in 100 Hz PIT ticks */
void     sys_sleep(unsigned ms);        /* sleep, ten seconds at most a call (v0.60.101) */
int      sys_random(void *buf, int len);  /* entropy-pool bytes, 256 at most a call (v0.60.118) */
/* Entry `index` of directory `path`: 1 written to *out, 0 past the end, -1 error. */
int  sys_readdir(const char *path, int index, udirent_t *out);
int  sys_unlink(const char *path);      /* 0, or -1 (missing, a bowl, not yours) */
int  sys_mkdir (const char *path);
int  sys_stat  (const char *path, ustat_t *out);   /* 0, or -1 (v0.59.0) */
/* Windows on the desktop (v0.60.146): see SYS_WIN_* in syscall_nr.h. */
int  sys_win_open (int w, int h, const char *title);           /* an id, or -1 */
int  sys_win_put  (int id, const unsigned *px);                /* the whole body; 0, or -1 */
int  sys_win_event(int id, uwinev_t *ev, unsigned wait_ms);    /* 1, 0 none, -1 gone */
int  sys_win_close(int id);
__attribute__((noreturn)) void sys_exit(int code);

/* conveniences */
int   strlen_(const char *s);
void  print(const char *s);          /* -> screen (fd 1)  */
void  eprint(const char *s);         /* -> serial (fd 2)  */
void  print_int(int v);              /* decimal, to screen */
void  eprint_int(int v);             /* decimal, to serial */
void *malloc(unsigned n);            /* bump allocator over sbrk */

/* The value of an environment variable the shell handed this program
 * (`hand NAME`), or 0 if it has none (v0.60.26). */
const char *getenv(const char *name);
