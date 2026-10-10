/* fwcfg.c - see fwcfg.h. The legacy I/O interface: a 16-bit selector
 * written to 0x510, the item's bytes read one at a time from 0x511. Item
 * 0 is the "QEMU" signature, item 0x19 the file directory: a big-endian
 * count, then 64-byte entries (size, select, reserved, a 56-byte name). */
#include "fwcfg.h"
#include <stdint.h>
#include "string.h"

static inline void sel(uint16_t s) { __asm__ volatile ("outw %0,%1" :: "a"(s), "Nd"((uint16_t)0x510)); }
static inline uint8_t rd(void) { uint8_t v; __asm__ volatile ("inb %1,%0" : "=a"(v) : "Nd"((uint16_t)0x511)); return v; }

int fwcfg_has(const char *name) {
    sel(0x0000);
    if (rd() != 'Q' || rd() != 'E' || rd() != 'M' || rd() != 'U') return 0;
    sel(0x0019);
    uint32_t n = 0;
    for (int i = 0; i < 4; i++) n = n << 8 | rd();
    if (n > 4096) return 0;
    for (uint32_t e = 0; e < n; e++) {
        char f[57];
        for (int i = 0; i < 8; i++) rd();              /* size, select, reserved */
        for (int i = 0; i < 56; i++) f[i] = (char)rd();
        f[56] = '\0';
        if (!strcmp(f, name)) return 1;
    }
    return 0;
}
