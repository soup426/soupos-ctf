#include "gdt.h"

/* A GDT entry is 8 bytes packed in a specific bit layout.
   We use a packed struct to build it. */
typedef struct {
    uint16_t limit_low;    /* bits 0-15 of segment limit */
    uint16_t base_low;     /* bits 0-15 of base address  */
    uint8_t  base_mid;     /* bits 16-23 of base         */
    uint8_t  access;       /* access byte (present, DPL, type) */
    uint8_t  gran;         /* granularity: limit_high (4 bits) + flags (4 bits) */
    uint8_t  base_high;    /* bits 24-31 of base         */
} __attribute__((packed)) gdt_entry_t;

/* The GDT register descriptor loaded by lgdt */
typedef struct {
    uint16_t limit;        /* size of GDT in bytes - 1 */
    uint32_t base;         /* linear address of GDT    */
} __attribute__((packed)) gdt_ptr_t;

/* null, kernel code, kernel data, user code, user data, TSS */
#define GDT_ENTRIES 6
static gdt_entry_t gdt[GDT_ENTRIES];
static gdt_ptr_t   gdt_ptr;

/* 32-bit Task State Segment. We only use it to hold the ring-0 stack
 * (ss0/esp0) the CPU loads on a privilege increase; no hardware task
 * switching. iomap_base = sizeof means "no I/O permission bitmap". */
typedef struct {
    uint32_t prev_tss;
    uint32_t esp0, ss0;
    uint32_t esp1, ss1, esp2, ss2;
    uint32_t cr3, eip, eflags;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap, iomap_base;
} __attribute__((packed)) tss_entry_t;

static tss_entry_t tss;
/* A dedicated ring-0 stack used when an interrupt/syscall enters from ring 3.
 * 16 KB, 16-aligned. esp0 points at the top (stacks grow down). */
static uint8_t kstack[16384] __attribute__((aligned(16)));

/* Defined in gdt_flush.asm */
extern void gdt_flush(uint32_t gdt_ptr_addr);

static void gdt_set_entry(int i, uint32_t base, uint32_t limit,
                          uint8_t access, uint8_t gran) {
    gdt[i].base_low  = (uint16_t)(base & 0xFFFF);
    gdt[i].base_mid  = (uint8_t)((base >> 16) & 0xFF);
    gdt[i].base_high = (uint8_t)((base >> 24) & 0xFF);

    gdt[i].limit_low = (uint16_t)(limit & 0xFFFF);
    gdt[i].gran      = (uint8_t)((gran & 0xF0) | ((limit >> 16) & 0x0F));

    gdt[i].access    = access;
}

void gdt_init(void) {
    gdt_ptr.limit = (uint16_t)(sizeof(gdt) - 1);
    gdt_ptr.base  = (uint32_t)&gdt;

    /* Entry 0: null descriptor (required by x86 spec) */
    gdt_set_entry(0, 0, 0, 0, 0);

    /* Entry 1: kernel code segment
       base=0, limit=4GB, ring 0, executable, readable
       access = 0x9A = 1001_1010b
         bit7 (present=1) bit6-5 (DPL=00) bit4 (S=1, code/data)
         bit3 (executable=1) bit2 (direction=0) bit1 (readable=1) bit0 (accessed=0)
       gran   = 0xCF = 1100_1111b
         bit7 (granularity=1 -> limit in 4KB pages) bit6 (32-bit=1) bit5-4 (0)
         bits3-0 = upper 4 bits of limit = 0xF  -> limit = 0xFFFFF pages = 4GB */
    gdt_set_entry(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);

    /* Entry 2: kernel data segment
       access = 0x92 = 1001_0010b (same as code but executable=0, writable=1) */
    gdt_set_entry(2, 0, 0xFFFFFFFF, 0x92, 0xCF);

    /* Entry 3: user code - like kernel code but DPL=3.
       access = 0xFA = 1111_1010b (present, DPL=11, S, exec, readable) */
    gdt_set_entry(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);

    /* Entry 4: user data - DPL=3.  access = 0xF2 = 1111_0010b */
    gdt_set_entry(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);

    /* Entry 5: the TSS.  access = 0x89 = present, DPL0, type 0x9
       (32-bit TSS, available).  Byte-granular limit = sizeof(tss)-1. */
    for (uint32_t i = 0; i < sizeof(tss); i++) ((uint8_t *)&tss)[i] = 0;
    tss.ss0         = 0x10;                      /* kernel data segment   */
    tss.esp0        = (uint32_t)(kstack + sizeof(kstack));
    tss.iomap_base  = (uint16_t)sizeof(tss_entry_t);
    gdt_set_entry(5, (uint32_t)&tss, sizeof(tss) - 1, 0x89, 0x00);

    gdt_flush((uint32_t)&gdt_ptr);

    /* Load the task register with the TSS selector. */
    __asm__ volatile ("ltr %0" : : "r"((uint16_t)0x28));
}

void tss_set_esp0(uint32_t esp0) {
    tss.esp0 = esp0;
}

/* Top of the boot ring-0 stack. Used as tss.esp0 whenever the task being
 * switched to has never entered ring 3: nothing can trap from ring 3 under it,
 * so the value is never actually consulted, but leaving a finished process's
 * frame address in the TSS is a trap waiting for the next bug. */
uint32_t tss_boot_esp0(void) {
    return (uint32_t)(kstack + sizeof(kstack));
}
