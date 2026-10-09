#include "pmm.h"
#include "multiboot.h"

#define PAGE_SIZE  4096
#define MAX_PAGES  (256 * 1024)    /* covers up to 1 GB */

/* The bitmap is shared mutable state, and since v0.8.0 several processes can
 * be inside the allocator at once: every proc_spawn calls paging_new_dir, and
 * every ELF load and sbrk calls map_user_page. The check-and-claim below is a
 * read-modify-write, so without this a timer preemption landing between
 * is_used() and set_used() hands the SAME physical frame to two processes,
 * which is silent cross-process memory corruption. used_pages_++ has the same
 * problem and is what the gate's leak check reads.
 *
 * Interrupts off rather than a mutex, matching heap.c: the critical sections
 * here are a few instructions, they never yield, and nothing in an interrupt
 * handler allocates, so there is nothing to block on. */
static inline uint32_t irq_save(void) {
    uint32_t f;
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(f) :: "memory");
    return f;
}
static inline void irq_restore(uint32_t f) {
    __asm__ volatile ("push %0; popf" :: "r"(f) : "memory", "cc");
}

/* Bitmap: bit=1 means page is used */
static uint32_t bitmap[MAX_PAGES / 32];   /* 32 KB in BSS */
static uint32_t total_pages_ = 0;
static uint32_t used_pages_  = 0;

/* Provided by linker.ld - address immediately after the kernel image */
extern char kernel_end;

static void set_used(uint32_t p) { bitmap[p / 32] |=  (1u << (p % 32)); }
static void set_free(uint32_t p) { bitmap[p / 32] &= ~(1u << (p % 32)); }
static int  is_used (uint32_t p) { return (bitmap[p / 32] >> (p % 32)) & 1; }

void pmm_init(uint32_t mb_info_addr) {
    multiboot_info_t *mb = (multiboot_info_t *)mb_info_addr;

    /* Start with everything marked used; free only what the mmap says is RAM */
    for (int i = 0; i < MAX_PAGES / 32; i++)
        bitmap[i] = 0xFFFFFFFF;

    if (!(mb->flags & (1 << 6)))
        return;   /* no memory map from bootloader - bail */

    uint32_t mmap_end = mb->mmap_addr + mb->mmap_length;
    multiboot_mmap_t *e = (multiboot_mmap_t *)(uintptr_t)mb->mmap_addr;

    while ((uint32_t)e < mmap_end) {
        /* Only touch regions that fit in 32-bit address space */
        if (e->addr_high == 0 && e->type == MULTIBOOT_MEMORY_AVAILABLE) {
            uint32_t start = e->addr_low;
            uint32_t len   = e->len_low;
            /* Round start up and end down to page boundaries */
            uint32_t first = (start + PAGE_SIZE - 1) / PAGE_SIZE;
            uint32_t last  = (start + len) / PAGE_SIZE;
            for (uint32_t p = first; p < last && p < MAX_PAGES; p++) {
                set_free(p);
                total_pages_++;
            }
        }
        /* Advance: e->size does not include the size field itself (+4) */
        e = (multiboot_mmap_t *)((uint32_t)e + e->size + 4);
    }

    /* Always keep page 0 reserved (guards against NULL dereferences) */
    if (!is_used(0)) { set_used(0); used_pages_++; }

    /* Reserve pages occupied by the kernel image */
    uint32_t kend = ((uint32_t)&kernel_end + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint32_t p = 1; p < kend && p < MAX_PAGES; p++) {
        if (!is_used(p)) {
            set_used(p);
            used_pages_++;
        }
    }
}

void *pmm_alloc_page(void) {
    uint32_t flags = irq_save();
    for (uint32_t i = 1; i < MAX_PAGES; i++) {
        if (!is_used(i)) {
            set_used(i);
            used_pages_++;
            irq_restore(flags);
            return (void *)(i * PAGE_SIZE);
        }
    }
    irq_restore(flags);
    return (void *)0;   /* out of memory */
}

void pmm_free_page(void *page) {
    uint32_t p = (uint32_t)page / PAGE_SIZE;
    uint32_t flags = irq_save();
    if (p > 0 && p < MAX_PAGES && is_used(p)) {
        set_free(p);
        used_pages_--;
    }
    irq_restore(flags);
}

uint32_t pmm_total_pages(void) { return total_pages_; }
uint32_t pmm_used_pages(void)  { return used_pages_; }
uint32_t pmm_free_pages(void)  { return total_pages_ - used_pages_; }
uint32_t pmm_total_kb(void)    { return total_pages_ * (PAGE_SIZE / 1024); }
uint32_t pmm_used_kb(void)     { return used_pages_  * (PAGE_SIZE / 1024); }
uint32_t pmm_free_kb(void)     { return pmm_free_pages() * (PAGE_SIZE / 1024); }
