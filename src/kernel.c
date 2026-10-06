#include "vga.h"
#include "console.h"
#ifndef NO_CHALLENGE
#include "challenge.h"
#endif
#include "gdt.h"
#include "idt.h"
#include "isr.h"
#include "keyboard.h"
#include "shell.h"
#include "timer.h"
#include "pmm.h"
#include "paging.h"
#include "heap.h"
#include "ata.h"
#include "fat.h"
#include "vfs.h"
#include "task.h"
#include "serial.h"
#include "klog.h"
#include "users.h"
#include "ai.h"
#include "usermode.h"

/* Read a byte from an I/O port - used for the PIC-mask sanity check. */
static inline uint8_t inb(uint16_t port) {
    uint8_t r; __asm__ volatile ("inb %1, %0" : "=a"(r) : "Nd"(port)); return r;
}

/* ---- scheduler bring-up self-test ----
 * Spawns a short-lived counter task that yields between prints. The
 * kernel main thread drives the scheduler with a handful of yields,
 * then proceeds to shell_run. Proves the context switch works end-to-end. */
static void counter_task(void *arg) {
    (void)arg;
    for (int i = 1; i <= 5; i++) {
        vga_printf("  [task %u:%s] tick %d\n",
                   task_current()->id, task_current()->name, i);
        serial_putc('A' + (i - 1));   /* A,B,C,D,E */
        task_yield();
    }
    vga_puts("  [task counter] done, exiting\n");
    klog_puts("\n[boot] counter task exited\n");
}
void kernel_main(uint32_t mb_magic, uint32_t mb_info) {
    vga_init();
    serial_init();
    console_init();          /* COM1 console: input + output mirror */
    klog_init();
    klog_puts("[boot] kernel_main\n");

    gdt_init();      klog_puts("[boot] GDT ok\n");
    idt_init();      klog_puts("[boot] IDT ok\n");
    timer_init();    klog_puts("[boot] timer ok\n");
    keyboard_init(); klog_puts("[boot] keyboard ok\n");

    pmm_init(mb_info);
    if (pmm_total_pages() == 0) {
        /* No memory map from bootloader, or all regions were reserved.
         * Enabling paging now would page-fault before we can recover. */
        klog_puts("[boot] FATAL: PMM found 0 usable pages - halting\n");
        vga_set_color(VGA_WHITE, VGA_RED);
        vga_puts("\n  *** BOOT PANIC ***\n  PMM: no usable RAM "
                 "(missing multiboot memory map)\n  System halted.\n");
        __asm__ volatile ("cli; hlt");
        while (1) {}
    }
    klog_puts("[boot] PMM ok\n");

    paging_init();
    klog_puts("[boot] paging on\n");

    heap_init();
    klog_puts("[boot] heap ok\n");

    __asm__ volatile ("sti");
    klog_puts("[boot] interrupts enabled\n");

    int ata_ok = (ata_init() == 0);
    if (ata_ok) klog_puts("[boot] ATA ok\n");
    else        klog_puts("[boot] ATA: no drive\n");

    int fat_ok = ata_ok && (fat_init() == 0);
    if (fat_ok) klog_puts("[boot] FAT ok\n");
    else if (ata_ok) klog_puts("[boot] FAT: not a FAT16/32 volume\n");

    /* VFS shim - uniform open/read/write over FAT files + /dev nodes.
     * Safe to init even without a disk: the /dev backends still work. */
    vfs_init();
    klog_puts("[boot] VFS ok\n");

    /* Kitchen staff - loads /etc/kitchen, seeding a default headchef. */
    users_init();
    klog_puts("[boot] users ok\n");

#ifndef NO_CHALLENGE
    challenge_init();
    klog_puts("[boot] challenge ok\n");
#endif

    /* AI serial bridge on COM2 (present only when QEMU was given a COM2
     * backend, e.g. `make run-ai`). Safe to probe unconditionally. */
    ai_init();
    klog_puts(ai_available() ? "[boot] ai bridge ready (COM2)\n"
                             : "[boot] ai bridge off (no COM2)\n");

    /* Prove the int 0x80 syscall path works (ring 0 -> dispatcher -> COM1). */
    usermode_selftest();
    klog_puts("[boot] syscall gate ok (int 0x80)\n");

    /* Check PIC masks */
    uint8_t m1 = inb(0x21), m2 = inb(0xA1);
    if (m1 & 0x01) klog_puts("[boot] WARNING: IRQ0 (timer) masked\n");
    if (m1 & 0x02) klog_puts("[boot] WARNING: IRQ1 (keyboard) masked\n");
    (void)m2;

    /* ---- Boot banner ---- */
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts(" ____                        ___  ____\n");
    vga_puts("/ ___|  ___  _   _ _ __     / _ \\/ ___|\n");
    vga_puts("\\___ \\ / _ \\| | | | '_ \\   | | | \\___ \\\n");
    vga_puts(" ___) | (_) | |_| | |_) |  | |_| |___) |\n");
    vga_puts("|____/ \\___/ \\__,_| .__/    \\___/|____/\n");
    vga_puts("                  |_|\n");

    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_puts("                 a shitty kernel\n\n");

    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    if (mb_magic == 0x2BADB002) {
        vga_puts("[OK] Multiboot handoff verified\n");
    } else {
        vga_set_color(VGA_YELLOW, VGA_BLACK);
        vga_printf("[WARN] Unexpected Multiboot magic: 0x%x\n", mb_magic);
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    }

    vga_puts("[OK] GDT loaded (flat 32-bit, ring 0)\n");
    vga_puts("[OK] IDT loaded (32 exceptions + 16 IRQs)\n");
    vga_puts("[OK] 8259 PIC remapped (IRQs -> vectors 32-47)\n");
    vga_puts("[OK] Timer running at 100 Hz (IRQ0)\n");
    vga_puts("[OK] Keyboard driver ready (IRQ1)\n");
    vga_printf("[OK] PMM ready     - %u KB free / %u KB total\n",
               pmm_free_kb(), pmm_total_kb());
    vga_printf("[OK] Paging on     - %u MB identity-mapped\n",
               pmm_total_kb() / 1024);
    vga_printf("[OK] Heap ready    - %u KB\n",
               heap_total_bytes() / 1024);

    if (ata_ok) {
        vga_printf("[OK] ATA drive     - %u MB\n",
                   ata_total_sectors() / 2048);
        if (fat_ok)
            vga_printf("[OK] FAT%d volume   - %s\n",
                       fat_get_type(), fat_label());
        else
            vga_puts("[--] No FAT filesystem on primary master\n");
    } else {
        vga_puts("[--] No ATA drive detected\n");
    }
    vga_puts("[OK] VFS ready     - files + /dev/{null,zero,serial,kbd}\n");
    vga_puts("[OK] Kernel log    - ring buffer + serial mirror (see: dmesg)\n");

    /* ---- Scheduler bring-up ---- */
    task_init();
    klog_puts("[boot] scheduler ok (kernel is task 0)\n");
    vga_printf("[OK] Scheduler ok  - kernel is task %u (%s)\n",
               task_current()->id, task_current()->name);

    vga_puts("\n[boot] scheduler self-test:\n");
    task_spawn("counter", counter_task, 0);
    klog_puts("[boot] spawned counter task, driving scheduler...\n");
    for (int i = 0; i < 10; i++) task_yield();
    klog_puts("[boot] scheduler self-test complete\n");
    vga_printf("[boot] self-test complete, tasks alive: %u\n\n",
               task_count());

    /* Arm timer-driven preemption now that the cooperative self-test is done.
     * From here a task that never yields can still be switched out by IRQ0. */
    task_preempt_enable();
    klog_puts("[boot] preemption enabled (IRQ0 @ 100 Hz)\n");

    vga_puts("Type 'menu' to see available commands.\n\n");

    shell_run();
}
