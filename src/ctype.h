#pragma once
/* ctype.h - hosted-C shim for the Doom port. */

#include "doom_libc.h"

#define isdigit(c)  doom_isdigit(c)
#define isalpha(c)  doom_isalpha(c)
#define isalnum(c)  doom_isalnum(c)
#define isspace(c)  doom_isspace(c)
#define isupper(c)  doom_isupper(c)
#define islower(c)  doom_islower(c)
#define isprint(c)  doom_isprint(c)
#define toupper(c)  doom_toupper(c)
#define tolower(c)  doom_tolower(c)
