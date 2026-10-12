/* fix.c - sine and cosine from a quarter-wave table (v0.60.192). */
#include "fix.h"
#include "sintab.h"
fx fx_sin(int a) {
    a &= 1023;
    if (a < 256) return sintab[a];
    if (a < 512) return sintab[512 - a];
    if (a < 768) return -sintab[a - 512];
    return -sintab[1024 - a];
}
fx fx_cos(int a) { return fx_sin(a + 256); }
