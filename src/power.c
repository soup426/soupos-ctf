/* power.c - see power.h. Measured on QEMU's i440fx machine (`-machine pc`,
 * SeaBIOS) before relying on it: SeaBIOS puts the PIIX4's power-management
 * I/O at 0x600, and QEMU's DSDT gives S5 a sleep type of 0, so SLP_EN alone
 * (0x2000) written to PM1a_CNT (0x604) turns the machine off and QEMU
 * exits. Other machines ignore the writes and reach the halt. */
#include "power.h"
#include <stdint.h>
#include "klog.h"

static inline void outw_(uint16_t p, uint16_t v) { __asm__ volatile ("outw %0,%1" :: "a"(v), "Nd"(p)); }
static inline void outb_(uint16_t p, uint8_t v)  { __asm__ volatile ("outb %0,%1" :: "a"(v), "Nd"(p)); }

void power_off(void) {
    klog("[power] off\n");
    outw_(0x604, 0x2000);
    outw_(0xB004, 0x2000);
    for (;;) __asm__ volatile ("cli; hlt");
}

void power_reboot(void) {
    klog("[power] reboot\n");
    uint8_t s;
    do { __asm__ volatile ("inb $0x64, %0" : "=a"(s)); } while (s & 0x02);
    outb_(0x64, 0xFE);
    __asm__ volatile (                     /* still here: an empty IDT and a fault */
        "subl $6, %%esp\n"
        "movw $0, (%%esp)\n"
        "movl $0, 2(%%esp)\n"
        "lidt (%%esp)\n"
        "int $0x00\n"
        : : : "memory"
    );
    for (;;) {}
}
