#pragma once
/* stdlib.h - hosted-C shim for the Doom port.
 * Maps the standard stdlib API to doom_libc equivalents. */

#include "doom_libc.h"

#define RAND_MAX DOOM_RAND_MAX

#define malloc(sz)        doom_malloc((uint32_t)(sz))
#define free(p)           doom_free(p)
#define realloc(p, sz)    doom_realloc(p, (uint32_t)(sz))
#define calloc(n, sz)     doom_calloc((uint32_t)(n), (uint32_t)(sz))

#define exit(code)        doom_exit(code)
#define abort()           doom_abort()

#define atoi(s)           doom_atoi(s)
#define atol(s)           doom_atol(s)
#define strtol(s, e, b)   doom_strtol(s, e, b)
#define strtoul(s, e, b)  doom_strtoul(s, e, b)

#define rand()            doom_rand()
#define srand(s)          doom_srand(s)

#define getenv(n)         doom_getenv(n)
#define system(cmd)       doom_system(cmd)

#define abs(x)            doom_abs(x)
#define labs(x)           doom_labs(x)
