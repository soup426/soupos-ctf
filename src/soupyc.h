#pragma once

/* Run a soupyc program from a null-terminated source string.
   Returns 0 on success, -1 on parse or runtime error. */
int soupyc_run(const char *source);
