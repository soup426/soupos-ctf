#include "ai.h"
#include "serial.h"
#include "timer.h"
#include "task.h"

void ai_init(void) {
    serial2_init();   /* sets the present-flag; no-op effect if absent */
}

int ai_available(void) {
    return serial2_ok();
}

void ai_send(const char *prompt) {
    if (!serial2_ok()) return;
    while (serial2_getc_nb() >= 0) {}   /* drop any stale reply bytes */
    serial2_puts(prompt);
    serial2_putc('\n');
}

int ai_getc(uint32_t timeout_ticks) {
    if (!serial2_ok()) return -1;
    uint32_t start = timer_get_ticks();
    for (;;) {
        int c = serial2_getc_nb();
        if (c >= 0) return c;
        if (timer_get_ticks() - start >= timeout_ticks) return -1;
        /* Yield so background tasks run, then park until the next timer IRQ
         * (~10 ms) instead of busy-spinning on the UART. */
        task_yield();
        cpu_halt();
    }
}
