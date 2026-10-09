#pragma once
#include <stdint.h>
#include <stddef.h>

/* The entropy pool, stage S2 of the SSH plan.
 *
 * Key generation needs unpredictable bytes, and a kernel that boots the same
 * way every time has very few. What soupOS can get at:
 *
 *   - RDRAND, when CPUID leaf 1 ECX bit 30 says the CPU has it. On real
 *     hardware and under KVM this is the strongest source by far, and it is
 *     mixed into every output block, not only at boot.
 *   - timing jitter: the TSC low word at every timer tick and every keyboard
 *     interrupt, which drift against each other unpredictably.
 *   - the wall clock from the RTC, once, so two boots a second apart start
 *     from different states even before any jitter arrives.
 *
 * Everything is folded through SHA-256 into a 32-byte pool. Output is a hash
 * of the pool and a counter, and the pool is re-hashed after every draw so a
 * leaked output does not reveal earlier ones.
 *
 * There is no test vector for randomness. `sample` checks that two draws
 * differ and that 64 KB of output passes a monobit and a runs count;
 * scripts/random-test.sh checks that two boots draw different bytes, which is
 * the property that actually matters and the one a deterministic pool fails. */

void random_init(void);
void random_stir(uint32_t v);               /* cheap; safe from an interrupt */
void random_bytes(void *buf, size_t n);
int  random_has_rdrand(void);
