#pragma once
/* stdio.h - hosted-C shim for the Doom port.
 * Maps the standard stdio API to doom_libc equivalents. */

#include "doom_libc.h"

typedef doom_FILE FILE;

#define stdin  doom_stdin
#define stdout doom_stdout
#define stderr doom_stderr

#define SEEK_SET DOOM_SEEK_SET
#define SEEK_CUR DOOM_SEEK_CUR
#define SEEK_END DOOM_SEEK_END

#define EOF (-1)

#define fopen(path, mode)         doom_fopen(path, mode)
#define fclose(fp)                doom_fclose(fp)
#define fread(buf, sz, cnt, fp)   doom_fread(buf, (uint32_t)(sz), (uint32_t)(cnt), fp)
#define fseek(fp, off, w)         doom_fseek(fp, (long)(off), w)
#define ftell(fp)                 doom_ftell(fp)
#define feof(fp)                  doom_feof(fp)
#define fgetc(fp)                 doom_fgetc(fp)
#define getc(fp)                  doom_fgetc(fp)
#define fprintf                   doom_fprintf
#define vfprintf                  doom_vfprintf
#define printf                    doom_printf
#define vprintf                   doom_vprintf
#define puts(s)                   doom_puts(s)
#define fputs(s, fp)              doom_fputs(s, fp)

/* snprintf / vsnprintf route to the kernel format engine */
#define snprintf(buf, n, ...)     ksnprintf(buf, (uint32_t)(n), __VA_ARGS__)
#define vsnprintf(buf, n, fmt, ap) kvsnprintf(buf, (uint32_t)(n), fmt, ap)
#define sprintf(buf, ...)         ksprintf(buf, __VA_ARGS__)
#define vsprintf(buf, fmt, ap)    kvsprintf(buf, fmt, ap)
