#include "paging.h"
#include "pmm.h"

#define PAGE_SIZE 4096

/* Page directory - must be 4KB-aligned.  Lives in BSS so it's zero-initialised
   before kernel_main is called, but paging_init zeroes it explicitly anyway. */
static uint32_t page_dir[1024] __attribute__((aligned(4096)));

/* The directory map_page currently operates on. Starts as the kernel's own so
 * paging_init works before any process exists. */
static uint32_t *cur_dir = page_dir;

/* ---------------------------------------------------------------------------
 * map_page - create one virtual->physical mapping.
 *
 * Called before paging is enabled, so all pointer arithmetic is physical.
 * After paging is on (via paging_map), identity-mapping keeps phys==virt so
 * the same arithmetic is still correct.
 * --------------------------------------------------------------------------- */
static void map_page(uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t pdi = virt >> 22;              /* top 10 bits: page-dir index  */
    uint32_t pti = (virt >> 12) & 0x3FF;   /* mid 10 bits: page-table index */

    if (!(cur_dir[pdi] & PAGE_PRESENT)) {
        /* Allocate a fresh page table from the physical memory manager */
        uint32_t *pt = (uint32_t *)pmm_alloc_page();
        if (!pt) return;    /* out of memory - silently skip */
        for (int i = 0; i < 1024; i++) pt[i] = 0;
        cur_dir[pdi] = (uint32_t)pt | PAGE_PRESENT | PAGE_WRITE;
    }

    /* A page is only user-accessible if BOTH its PDE and PTE allow it (the CPU
     * ANDs privilege across levels). Kernel PTEs keep U=0, so marking the PDE
     * user-accessible here never exposes kernel pages - it only lets a later
     * user PTE under the same 4 MB region take effect. */
    if (flags & PAGE_USER) cur_dir[pdi] |= PAGE_USER;

    uint32_t *pt = (uint32_t *)(cur_dir[pdi] & ~0xFFFu);
    pt[pti] = (phys & ~0xFFFu) | flags | PAGE_PRESENT;
}

/* ---------------------------------------------------------------------------
 * paging_unmap - drop a single mapping. Returns the physical frame that was
 * mapped (page-aligned), or 0 if the address was not present. Flushes the TLB.
 * --------------------------------------------------------------------------- */
uint32_t paging_unmap(uint32_t virt) {
    uint32_t pdi = virt >> 22;
    uint32_t pti = (virt >> 12) & 0x3FF;
    if (!(cur_dir[pdi] & PAGE_PRESENT)) return 0;
    uint32_t *pt = (uint32_t *)(cur_dir[pdi] & ~0xFFFu);
    uint32_t pte = pt[pti];
    if (!(pte & PAGE_PRESENT)) return 0;
    pt[pti] = 0;
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
    return pte & ~0xFFFu;
}

/* ---------------------------------------------------------------------------
 * paging_map - public API for mapping a single page after paging is on.
 * Flushes the TLB entry for virt via invlpg.
 * --------------------------------------------------------------------------- */
void paging_map(uint32_t virt, uint32_t phys, uint32_t flags) {
    map_page(virt, phys, flags);
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

/* ---------------------------------------------------------------------------
 * paging_init - identity-map all physical RAM then enable paging.
 *
 * Identity map means virtual address == physical address everywhere, so all
 * existing pointers (stack, kernel code, VGA buffer, PMM bitmap …) remain
 * valid after the switch.  Page 0 is intentionally left unmapped so that
 * NULL-pointer dereferences produce a page fault rather than silent corruption.
 *
 * Page tables are allocated on-the-fly from the PMM.  Each one covers 4 MB
 * (1024 × 4 KB), so 128 MB needs 32 page tables = 128 KB extra RAM.
 *
 * Ordering note: pmm_init() must be called first so that pmm_alloc_page()
 * works; interrupts must still be off (sti comes after this returns).
 * --------------------------------------------------------------------------- */
void paging_init(void) {
    for (int i = 0; i < 1024; i++)
        page_dir[i] = 0;

    /* total_pages is the count of pages the bootloader reported as usable RAM.
       Iterating 1..total_pages covers the full physical address range including
       the reserved holes (VGA at 0xB8000, etc.) because their page numbers are
       less than total_pages (~32 000 for a 128 MB VM). */
    uint32_t total = pmm_total_pages();
    for (uint32_t p = 1; p < total; p++)
        map_page(p * PAGE_SIZE, p * PAGE_SIZE, PAGE_PRESENT | PAGE_WRITE);

    /* Point CR3 at our page directory (physical address, no flags needed) */
    __asm__ volatile ("mov %0, %%cr3" : : "r"((uint32_t)page_dir) : "memory");

    /* Set the PG bit (31) in CR0 - paging is now active */
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000u;
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");
    /* The next instruction fetch goes through the MMU.  Because EIP is in the
       identity-mapped kernel region (virt == phys), execution continues without
       a fault. */
}

/* ── Address spaces ───────────────────────────────────────────────────────
 * The kernel is identity-mapped, so a directory's virtual address is also its
 * physical address and can go straight into CR3.
 *
 * Kernel PDEs are shared by pointer, not copied: every address space refers to
 * the same page tables below PAGING_USER_PDE, so a kernel mapping made in one
 * is immediately visible in all of them. Only the user half is private, which
 * is the whole point.
 */

uint32_t *paging_kernel_dir(void)  { return page_dir; }
uint32_t *paging_current_dir(void) { return cur_dir; }

uint32_t *paging_new_dir(void) {
    uint32_t *dir = (uint32_t *)pmm_alloc_page();
    if (!dir) return 0;
    for (int i = 0; i < 1024; i++) dir[i] = 0;
    /* Share the kernel half. */
    for (uint32_t i = 0; i < PAGING_USER_PDE; i++) dir[i] = page_dir[i];
    return dir;
}

void paging_free_dir(uint32_t *dir) {
    if (!dir || dir == page_dir) return;         /* never free the kernel's */
    if (dir == cur_dir) paging_switch(page_dir); /* do not free what CR3 uses */

    /* Only the user half is ours: the kernel tables are shared. */
    for (uint32_t i = PAGING_USER_PDE; i < 1024; i++) {
        if (dir[i] & PAGE_PRESENT) {
            uint32_t *pt = (uint32_t *)(dir[i] & ~0xFFFu);
            for (int j = 0; j < 1024; j++) {
                if (pt[j] & PAGE_PRESENT)
                    pmm_free_page((void *)(pt[j] & ~0xFFFu));
            }
            pmm_free_page((void *)pt);
        }
        dir[i] = 0;
    }
    pmm_free_page((void *)dir);
}

void paging_switch(uint32_t *dir) {
    if (!dir) return;
    cur_dir = dir;
    __asm__ volatile ("mov %0, %%cr3" : : "r"((uint32_t)dir) : "memory");
}

/* The CPU sets a PTE's accessed bit (0x20) whenever the page is used. Read
 * it and clear it, flushing the TLB entry so the next use sets it again:
 * the "second chance" the clock algorithm gives a page in use. */
int paging_test_and_clear_accessed(uint32_t virt) {
    uint32_t pdi = virt >> 22, pti = (virt >> 12) & 0x3FF;
    if (!(cur_dir[pdi] & PAGE_PRESENT)) return 0;
    uint32_t *pt = (uint32_t *)(cur_dir[pdi] & ~0xFFFu);
    if (!(pt[pti] & PAGE_PRESENT) || !(pt[pti] & 0x20)) return 0;
    pt[pti] &= ~0x20u;
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
    return 1;
}

int paging_is_mapped(uint32_t virt) {
    uint32_t pdi = virt >> 22;
    uint32_t pti = (virt >> 12) & 0x3FF;
    if (!(cur_dir[pdi] & PAGE_PRESENT)) return 0;
    uint32_t *pt = (uint32_t *)(cur_dir[pdi] & ~0xFFFu);
    return (pt[pti] & PAGE_PRESENT) ? 1 : 0;
}
