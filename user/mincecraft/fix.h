/* fix.h - 16.16 fixed point for Mincecraft (v0.60.192): ring 3 has no FPU. */
#pragma once
typedef int fx;
#define FX_ONE 65536
static inline fx fx_mul(fx a, fx b) { return (fx)(((long long)a * b) >> 16); }
static inline fx fx_div(fx a, fx b) { return (fx)(((long long)a << 16) / b); }
/* Angles in 1024ths of a turn. */
fx fx_sin(int a);
fx fx_cos(int a);
