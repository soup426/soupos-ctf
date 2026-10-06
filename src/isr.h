#pragma once
#include <stdint.h>

/* Pushed by the ISR stubs onto the stack before calling the C handler.
   Layout must exactly match what isr_stubs.asm pushes. */
typedef struct {
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
