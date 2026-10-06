#pragma once
/* errno.h - hosted-C shim for the Doom port. */

#include "doom_libc.h"

/* errno is defined in doom_libc.c; doom_libc.h declares it extern.
 * This header just makes #include <errno.h> work in Doom source files. */
