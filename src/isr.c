#include "isr.h"
#include "usermode.h"
#include "vga.h"
#include "klog.h"
#include "task.h"

/* I/O port helper */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static const char *exception_names[] = {
    "Division By Zero",          /*  0 */
    "Debug",                     /*  1 */
    "Non-Maskable Interrupt",    /*  2 */
    "Breakpoint",                /*  3 */
    "Overflow",                  /*  4 */
    "Bound Range Exceeded",      /*  5 */
    "Invalid Opcode",            /*  6 */
    "Device Not Available",      /*  7 */
    "Double Fault",              /*  8 */
    "Coprocessor Segment",       /*  9 */
    "Invalid TSS",               /* 10 */
    "Segment Not Present",       /* 11 */
    "Stack-Segment Fault",       /* 12 */
    "General Protection Fault",  /* 13 */
    "Page Fault",                /* 14 */
    "Reserved",                  /* 15 */
    "x87 Floating-Point",        /* 16 */
    "Alignment Check",           /* 17 */
    "Machine Check",             /* 18 */
    "SIMD Floating-Point",       /* 19 */
};

/* A frame address is "plausible" if it is 4-aligned and within the
 * identity-mapped low 128 MB - so dereferencing it during a panic can
 * never itself fault and turn the panic into a triple-fault reboot. */
static int frame_ok(uint32_t addr) {
    return addr >= 0x1000 && addr < 0x08000000 && (addr & 3) == 0;
}

/* Walk saved EBP frames from `ebp`, reporting return addresses. Each
 * frame is [saved ebp][return eip]; the stack grows down so a caller's
 * frame must sit at a higher address than the callee's. */
static void stack_trace(uint32_t ebp) {
    vga_puts("  stack trace (return addrs):\n");
    klog("stack trace (return addrs):\n");
    uint32_t *frame = (uint32_t *)ebp;
    for (int i = 0; i < 12; i++) {
        if (!frame_ok((uint32_t)frame)) break;
        uint32_t ret  = frame[1];
        uint32_t next = frame[0];
        vga_printf("    #%u  0x%x\n", (uint32_t)i, ret);
        klog("  #%u 0x%x\n", (uint32_t)i, ret);
        if (next <= (uint32_t)frame) break;   /* not unwinding upward */
        frame = (uint32_t *)next;
    }
}

/* Called from isr_stubs.asm for CPU exceptions (vectors 0-31).
 * Dumps to the VGA panic screen AND mirrors the whole report to the
 * kernel log / serial so a crash is recoverable from a capture. */
void isr_handler(registers_t *regs) {
    const char *name = (regs->int_no < 20)
                            ? exception_names[regs->int_no]
                            : "Unknown";

    /* A fault in ring 3 kills the program, not the machine. CPL lives in the
     * low two bits of the saved CS; ring 3 can only be reached by a user
     * program, and usermode_in_user() confirms one is actually running.
     * Without this, `cook`ing an ELF with a bad instruction panicked and
     * halted the whole box, which defeats the point of having ring 3. */
    if ((regs->cs & 3) == 3 && usermode_in_user()) {
        uint32_t cr2 = 0;
        if (regs->int_no == 14)
            __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_printf("\n  [program killed] exception %u: %s\n",
                   regs->int_no, name);
        vga_printf("  eip=0x%x err=0x%x", regs->eip, regs->err_code);
        if (regs->int_no == 14) vga_printf(" cr2=0x%x", cr2);
        vga_puts("\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        klog("[user] fault exc=%u err=0x%x eip=0x%x cr2=0x%x -> program killed\n",
             regs->int_no, regs->err_code, regs->eip, cr2);
        usermode_fault(regs->int_no, regs->err_code, regs->eip, cr2);
        /* not reached */
    }
    /* Ring 0 -> no privilege change -> the CPU did not push SS:ESP, so the
     * interrupted ESP is the address just past the CPU-pushed eflags. */
    uint32_t esp = (uint32_t)&regs->eflags + 4;

    vga_set_color(VGA_WHITE, VGA_RED);
    vga_printf("\n\n  *** KERNEL PANIC ***\n  Exception %u: %s\n",
               regs->int_no, name);
    klog("\n*** KERNEL PANIC *** exception %u: %s\n", regs->int_no, name);

    vga_printf("  err=0x%x  eip=0x%x  cs=0x%x  eflags=0x%x\n",
               regs->err_code, regs->eip, regs->cs, regs->eflags);
    klog("err=0x%x eip=0x%x cs=0x%x eflags=0x%x\n",
         regs->err_code, regs->eip, regs->cs, regs->eflags);

    vga_printf("  eax=0x%x ebx=0x%x ecx=0x%x edx=0x%x\n",
               regs->eax, regs->ebx, regs->ecx, regs->edx);
    klog("eax=0x%x ebx=0x%x ecx=0x%x edx=0x%x\n",
         regs->eax, regs->ebx, regs->ecx, regs->edx);

    vga_printf("  esi=0x%x edi=0x%x ebp=0x%x esp=0x%x\n",
               regs->esi, regs->edi, regs->ebp, esp);
    klog("esi=0x%x edi=0x%x ebp=0x%x esp=0x%x\n",
         regs->esi, regs->edi, regs->ebp, esp);

    if (regs->int_no == 14) {   /* Page fault: CR2 holds the faulting addr */
        uint32_t cr2;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        const char *c1 = (regs->err_code & 0x1) ? "protection" : "not-present";
        const char *c2 = (regs->err_code & 0x2) ? "write"      : "read";
        const char *c3 = (regs->err_code & 0x4) ? "user"       : "kernel";
        vga_printf("  fault addr=0x%x  (%s, %s, %s)\n", cr2, c1, c2, c3);
        klog("fault addr=0x%x (%s, %s, %s)\n", cr2, c1, c2, c3);
    }

    stack_trace(regs->ebp);

    vga_puts("\n  System halted.\n");
    klog("system halted\n");
    __asm__ volatile ("cli; hlt");
    while (1) {}
}

/* Table of registered IRQ handlers */
static irq_handler_t irq_handlers[16];

void irq_register(uint8_t irq, irq_handler_t handler) {
    if (irq < 16)
        irq_handlers[irq] = handler;
}

/* Called from isr_stubs.asm for hardware IRQs (vectors 32-47) */
void irq_handler(registers_t *regs) {
    uint8_t irq = (uint8_t)(regs->int_no - 32);

    /* Call registered handler if one exists */
    if (irq < 16 && irq_handlers[irq])
        irq_handlers[irq](regs);

    /* Send End-Of-Interrupt to PIC(s) */
    if (irq >= 8)
        outb(0xA0, 0x20);   /* slave PIC EOI */
    outb(0x20, 0x20);       /* master PIC EOI */

    /* Timer-driven preemption: do it AFTER the EOI (so the PIC is ready for
     * the next tick) and still with interrupts disabled. sched_preempt may
     * switch to another task; the full IRQ register frame on this task's stack
     * is preserved and consumed by iret when this task is later resumed. */
    if (irq == 0)
        sched_preempt();
}
