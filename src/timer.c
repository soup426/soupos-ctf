#include "timer.h"
#include "isr.h"

static volatile uint32_t ticks = 0;

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void timer_handler(registers_t *regs) {
    (void)regs;
    ticks++;
}

void timer_init(void) {
    uint32_t divisor = 1193182 / TIMER_HZ;
    outb(0x43, 0x36);                        /* ch 0, lo/hi, mode 3, binary */
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)(divisor >> 8));
    irq_register(0, timer_handler);
}

uint32_t timer_get_ticks(void)   { return ticks; }
uint32_t timer_get_seconds(void) { return ticks / TIMER_HZ; }
