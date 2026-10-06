#pragma once
/* doom_libc.h - declarations for the C library shim used by the Doom port.
 *
 * Doom source files should NOT include <stdio.h> etc. directly.  Instead
 * they include the thin wrappers in src/stdio.h, src/stdlib.h, etc. which
 * all pull in this header and map the standard names to the doom_* symbols
 * defined here.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

/* ── errno ─────────────────────────────────────────────────────────────── */
extern int errno;
#define ENOENT  2
#define ENOMEM  12
#define EACCES  13
#define EINVAL  22
#define ENOSPC  28

/* ── kvsnprintf / ksnprintf ─────────────────────────────────────────────── */
int kvsnprintf(char *buf, uint32_t size, const char *fmt, va_list ap);
int kvsprintf (char *buf,                const char *fmt, va_list ap);
int ksnprintf (char *buf, uint32_t size, const char *fmt, ...);
int ksprintf  (char *buf,                const char *fmt, ...);

/* ── FILE ───────────────────────────────────────────────────────────────── */
typedef struct {
    int fd;
    int eof;
} doom_FILE;

extern doom_FILE *doom_stdout;
extern doom_FILE *doom_stderr;
extern doom_FILE *doom_stdin;

doom_FILE *doom_fopen  (const char *path, const char *mode);
int        doom_fclose (doom_FILE *fp);
uint32_t   doom_fread  (void *buf, uint32_t size, uint32_t count, doom_FILE *fp);
int        doom_fseek  (doom_FILE *fp, long offset, int whence);
long       doom_ftell  (doom_FILE *fp);
int        doom_feof   (doom_FILE *fp);
int        doom_fgetc  (doom_FILE *fp);
int        doom_fprintf(doom_FILE *fp, const char *fmt, ...);
int        doom_vfprintf(doom_FILE *fp, const char *fmt, va_list ap);
int        doom_printf (const char *fmt, ...);
int        doom_vprintf(const char *fmt, va_list ap);
int        doom_puts   (const char *s);
int        doom_fputs  (const char *s, doom_FILE *fp);

/* SEEK constants (mirror FAT_SEEK_*) */
#define DOOM_SEEK_SET 0
#define DOOM_SEEK_CUR 1
#define DOOM_SEEK_END 2

/* ── memory ─────────────────────────────────────────────────────────────── */
void *doom_malloc (uint32_t size);
void  doom_free   (void *p);
void *doom_realloc(void *p, uint32_t size);
void *doom_calloc (uint32_t n, uint32_t size);

/* ── stdlib helpers ─────────────────────────────────────────────────────── */
void          doom_exit   (int code);
void          doom_abort  (void);
int           doom_atoi   (const char *s);
long          doom_atol   (const char *s);
long          doom_strtol (const char *s, char **end, int base);
unsigned long doom_strtoul(const char *s, char **end, int base);
int           doom_rand   (void);
void          doom_srand  (unsigned s);
char         *doom_getenv (const char *name);
int           doom_system (const char *cmd);

#define DOOM_RAND_MAX 0x7FFF

/* ── time ───────────────────────────────────────────────────────────────── */
long doom_time(long *t);

/* ── integer math ───────────────────────────────────────────────────────── */
int  doom_abs  (int  x);
long doom_labs (long x);
int  doom_isqrt(int  x);

/* ── ctype ──────────────────────────────────────────────────────────────── */
int doom_isdigit(int c);
int doom_isalpha(int c);
int doom_isalnum(int c);
int doom_isspace(int c);
int doom_isupper(int c);
int doom_islower(int c);
int doom_isprint(int c);
int doom_toupper(int c);
int doom_tolower(int c);

/* ── extra string functions ─────────────────────────────────────────────── */
char *doom_strdup  (const char *s);
char *doom_strupr  (char *s);
char *doom_strlwr  (char *s);
char *doom_strrchr (const char *s, int c);
char *doom_strncat (char *dst, const char *src, uint32_t n);
