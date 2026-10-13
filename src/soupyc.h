#pragma once

/* Run a soupyc program from a null-terminated source string.
   Returns 0 on success, -1 on parse or runtime error. */
int soupyc_run(const char *source);

/* A killed spawned script's context: its files closed, the rest freed (v0.60.290). */
void soupyc_release_killed(void *ctx);
