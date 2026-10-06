#include "heap.h"
#include "str.h"

/* ---------------------------------------------------------------------------
 * Simple first-fit heap allocator.
 *
 * Backing store is a 1 MB static array in BSS.  The PMM sees it as part of
 * the kernel image (it falls before kernel_end in the linker script) so those
 * pages are pre-marked used - no PMM interaction needed at runtime.
 *
 * Block layout (12 bytes of overhead per allocation):
 *
 *   [block_t header | ... usable bytes ...]
 *
 * Allocation: first-fit scan; split the tail if there is room for a new
 *             header plus at least 4 usable bytes.
 * Free:       mark free, then coalesce with contiguous free neighbours on
 *             both sides. Backward-coalesce walks from heap_head to find
 *             the predecessor - O(n) per free, but keeps the list short.
 * --------------------------------------------------------------------------- */

#define HEAP_SIZE (8u * 1024u * 1024u)   /* 8 MB - enough for Doom's zone allocator */
#define MIN_SPLIT 4u                /* minimum usable bytes in a split tail */

typedef struct block {
    uint32_t      size;   /* usable bytes after this header (never includes header) */
    uint32_t      free;   /* 1 = free, 0 = in use */
    struct block *next;   /* next block in the list, NULL if last */
} block_t;

/* 8 MB backing store - 4 KB aligned, zero-initialised by the BSS rules */
static uint8_t heap_arena[HEAP_SIZE] __attribute__((aligned(4096)));
static block_t *heap_head = (void *)0;

/* The free list is shared mutable state, so a preemptive task switch in the
 * middle of an allocation could corrupt it. These bracket each heap operation
 * with interrupts disabled (restoring the prior IF), making it atomic with
 * respect to the timer IRQ that drives preemption. Cheap - heap ops are short. */
static inline uint32_t irq_save(void) {
    uint32_t f;
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(f) :: "memory");
    return f;
}
static inline void irq_restore(uint32_t f) {
    __asm__ volatile ("push %0; popf" :: "r"(f) : "memory", "cc");
}

void heap_init(void) {
    heap_head        = (block_t *)heap_arena;
    heap_head->size  = HEAP_SIZE - sizeof(block_t);
    heap_head->free  = 1;
    heap_head->next  = (void *)0;
}

void *kmalloc(uint32_t size) {
    if (!size || !heap_head) return (void *)0;

    /* Round up to 4-byte boundary so returned pointers stay aligned */
    size = (size + 3u) & ~3u;

    uint32_t flags = irq_save();
    for (block_t *b = heap_head; b; b = b->next) {
        if (!b->free || b->size < size)
            continue;

        /* Split if the leftover would hold a header plus MIN_SPLIT bytes */
        if (b->size >= size + sizeof(block_t) + MIN_SPLIT) {
            block_t *tail  = (block_t *)((uint8_t *)b + sizeof(block_t) + size);
            tail->size     = b->size - size - sizeof(block_t);
            tail->free     = 1;
            tail->next     = b->next;
            b->size        = size;
            b->next        = tail;
        }

        b->free = 0;
        void *r = (void *)((uint8_t *)b + sizeof(block_t));
        irq_restore(flags);
        return r;
    }
    irq_restore(flags);
    return (void *)0;   /* heap exhausted */
}

void kfree(void *ptr) {
    if (!ptr) return;

    uint32_t flags = irq_save();
    block_t *b = (block_t *)((uint8_t *)ptr - sizeof(block_t));
    b->free = 1;

    /* Forward-coalesce: absorb consecutive free blocks into b */
    while (b->next && b->next->free) {
        b->size += sizeof(block_t) + b->next->size;
        b->next  = b->next->next;
    }

    /* Backward-coalesce: find predecessor and absorb b into it if free */
    if (b != heap_head) {
        block_t *prev = heap_head;
        while (prev && prev->next != b) prev = prev->next;
        if (prev && prev->free) {
            prev->size += sizeof(block_t) + b->size;
            prev->next  = b->next;
        }
    }
    irq_restore(flags);
}

/* Return the usable size of an active allocation (reads the block header).
   Returns 0 for NULL or already-freed pointers. */
uint32_t kmalloc_size(void *ptr) {
    if (!ptr) return 0;
    block_t *b = (block_t *)((uint8_t *)ptr - sizeof(block_t));
    return b->free ? 0 : b->size;
}

void *krealloc(void *ptr, uint32_t size) {
    if (!ptr)  return kmalloc(size);
    if (!size) { kfree(ptr); return (void *)0; }
    uint32_t old = kmalloc_size(ptr);
    if (old >= size) return ptr;   /* already big enough - no-op */
    void *newptr = kmalloc(size);
    if (!newptr) return (void *)0;
    memcpy(newptr, ptr, old);
    kfree(ptr);
    return newptr;
}

void *kcalloc(uint32_t n, uint32_t size) {
    uint32_t total = n * size;
    void *p = kmalloc(total);
    if (p) memset(p, 0, total);
    return p;
}

/* --- stats ---------------------------------------------------------------- */

uint32_t heap_used_bytes(void) {
    uint32_t total = 0;
    for (block_t *b = heap_head; b; b = b->next)
        if (!b->free) total += b->size;
    return total;
}

uint32_t heap_free_bytes(void) {
    uint32_t total = 0;
    for (block_t *b = heap_head; b; b = b->next)
        if (b->free) total += b->size;
    return total;
}

uint32_t heap_total_bytes(void) {
    return HEAP_SIZE - sizeof(block_t);   /* usable bytes in the arena */
}
