#pragma once
/* math.h - hosted-C shim for the Doom port.
 * Doom's renderer uses only integer math internally; floating-point
 * entry points are stubbed out (Doom never calls them in practice when
 * the WAD-based lookup tables are active). */

#include "doom_libc.h"

/* Integer variants used by Doom */
#define abs(x)    doom_abs(x)

/* Floating-point stubs - provided so the translation unit compiles even
 * if a header pulls in a float prototype.  Doom should not call these
 * at runtime when running with pre-computed tables. */
static inline double floor(double x)  { return (double)(long long)x; }
static inline double ceil (double x)  {
    long long i = (long long)x;
    return (double)(i + (x > (double)i ? 1 : 0));
}
static inline double fabs (double x)  { return x < 0.0 ? -x : x; }
static inline double sqrt (double x)  { return (double)doom_isqrt((int)(x * 65536)) / 256.0; }

/* Trig stubs - Doom uses lookup tables (finesine[]) so these are never
 * called once the WAD is loaded.  Returning 0.0 is safe for startup. */
static inline double sin(double x)    { (void)x; return 0.0; }
static inline double cos(double x)    { (void)x; return 1.0; }
static inline double atan2(double y, double x) { (void)y; (void)x; return 0.0; }
static inline double log(double x)    { (void)x; return 0.0; }
static inline double exp(double x)    { (void)x; return 1.0; }
static inline double pow(double b, double e) { (void)b; (void)e; return 1.0; }
