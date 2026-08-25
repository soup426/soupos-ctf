#pragma once
#include <stdint.h>

/* AlphaSOUP-32: a non-cryptographic hash for soupOS.
 *
 * Think of each byte as a letter of pasta getting stirred
 * into a pot - every letter changes the flavor (hash state).
 *
 * Properties: fast, good avalanche, suitable for hash tables
 * and checksums.  NOT cryptographically secure.
 */
uint32_t alphasoup_hash(const void *data, uint32_t len);
