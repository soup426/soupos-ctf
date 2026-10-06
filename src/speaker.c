/* speaker.c - PC speaker driver for soupOS
 *
 * Uses PIT channel 2 as a tone generator:
 *   Port 0x43 (PIT command): 0xB6 = channel 2, mode 3 (square wave), binary
 *   Port 0x42 (channel 2 data): divisor low byte then high byte
 *   Port 0x61 bits 0-1: gate (bit 0) + speaker output enable (bit 1)
 *
 * Divisor = 1193182 / freq_hz
 */

#include "speaker.h"
#include "task.h"

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

void speaker_off(void) {
    /* Clear bits 0 and 1 of port 0x61 to cut the speaker */
    uint8_t b = inb(0x61);
    outb(0x61, b & ~0x03u);
}

void speaker_beep(uint32_t freq, uint32_t ms) {
    if (freq == 0 || ms == 0) { speaker_off(); return; }

    /* Clamp to audible range */
    if (freq < 20)    freq = 20;
    if (freq > 20000) freq = 20000;

    /* Program PIT channel 2 for a square wave at `freq` Hz */
    uint32_t divisor = 1193182u / freq;
    outb(0x43, 0xB6);                            /* channel 2, mode 3 */
    outb(0x42, (uint8_t)(divisor & 0xFF));        /* divisor low byte  */
    outb(0x42, (uint8_t)((divisor >> 8) & 0xFF)); /* divisor high byte */

    /* Enable gate + speaker output (bits 0 and 1 of port 0x61) */
    uint8_t b = inb(0x61);
    outb(0x61, b | 0x03u);

    /* Hold the tone for `ms` while letting other tasks run. task_sleep
     * marks this task BLOCKED until the deadline; before task_init() it
     * falls back to a hlt loop, so this is safe to call at any time. */
    task_sleep(ms);

    speaker_off();
}
