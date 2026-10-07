#pragma once
#include <stdint.h>

/* Pushed by the ISR stubs onto the stack before calling the C handler.
   Layout must exactly match what isr_stubs.asm pushes. */
typedef struct {
    /* Pushed by our stub, after pusha: the interrupted context's data
     * selector. The stub then loads kernel selectors, so the kernel always
     * runs with a known DS regardless of what it interrupted, and restores
     * this one on the way out. */
    uint32_t ds;
    /* Pushed by pusha */
    uint32_t edi, esi, ebp, esp_dummy;
    uint32_t ebx, edx, ecx, eax;
    /* Pushed by our stub */
    uint32_t int_no;
    uint32_t err_code;
    /* Pushed automatically by the CPU on interrupt */
    uint32_t eip, cs, eflags;
} __attribute__((packed)) registers_t;

typedef void (*irq_handler_t)(registers_t *);

void isr_handler(registers_t *regs);
void irq_handler(registers_t *regs);
void irq_register(uint8_t irq, irq_handler_t handler);

/* Open an IRQ line at the PIC. Only the timer and keyboard are open by
 * default; a slave line (8-15) also opens the cascade on the master. */
void irq_unmask(uint8_t irq);
