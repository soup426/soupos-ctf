#pragma once
#include <stddef.h>
#include <stdarg.h>

/* Standard memory/string functions provided by the kernel.
   Named without prefix so GCC's implicit calls (struct init, etc.) link. */

void  *memset (void *dst, int val, size_t n);
void  *memcpy (void *dst, const void *src, size_t n);
void  *memmove(void *dst, const void *src, size_t n);
int    memcmp (const void *a, const void *b, size_t n);

size_t strlen (const char *s);
char  *strcpy (char *dst, const char *src);
char  *strncpy(char *dst, const char *src, size_t n);
int    strcmp (const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);
char  *strcat (char *dst, const char *src);
char  *strchr (const char *s, int c);

/* The kernel's printf, minus the destination: emit(c, ctx) per output byte.
 * vga_printf and term_printf both use it, so they format identically. */
void kvformat(void (*emit)(char c, void *ctx), void *ctx, const char *fmt, va_list ap);
