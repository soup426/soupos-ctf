#pragma once
/* string.h - hosted-C shim for the Doom port.
 * Re-exports everything from str.h and adds the extras in doom_libc. */

#include "str.h"
#include "doom_libc.h"

/* str.h already declares: memset memcpy memmove memcmp
 *   strlen strcpy strncpy strcmp strncmp strcat strchr */

#define strdup(s)          doom_strdup(s)
#define strupr(s)          doom_strupr(s)
#define strlwr(s)          doom_strlwr(s)
#define strrchr(s, c)      doom_strrchr(s, c)
#define strncat(d, s, n)   doom_strncat(d, s, (uint32_t)(n))
