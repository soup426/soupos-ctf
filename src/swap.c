/* swap.c - pages on disk past the FAT volume. See swap.h. */
#include "swap.h"
#include "ata.h"
#include "fat.h"
#include "paging.h"
#include "pmm.h"
#include "heap.h"
#include "str.h"
#include "klog.h"

#define SECTORS_PER_PAGE 8
#define USER_HEAP_BASE   0xC4000000u          /* matches usermode.c */
#define MAX_HEAP_PAGES   16384                /* 64 MB of break, the table's size */

static uint32_t base_lba, nslots, used;
static uint8_t *bitmap;                       /* one bit per slot */

static int slot_taken(uint32_t s) { return bitmap[s >> 3] & (1u << (s & 7)); }
static void slot_set(uint32_t s, int on) {
    if (on) bitmap[s >> 3] |=  (uint8_t)(1u << (s & 7));
    else    bitmap[s >> 3] &= (uint8_t)~(1u << (s & 7));
}
static int slot_alloc(void) {
    for (uint32_t s = 0; s < nslots; s++)
        if (!slot_taken(s)) { slot_set(s, 1); used++; return (int)s; }
    return -1;
}
static void slot_free(uint32_t s) { if (slot_taken(s)) { slot_set(s, 0); used--; } }

void swap_init(void) {
    uint32_t vol = fat_volume_sectors(), disk = ata_total_sectors();
    if (!vol || disk <= vol + 256 * SECTORS_PER_PAGE) {
        klog("[swap] none: the volume fills the disk (%u of %u sectors)\n", vol, disk);
        return;
    }
    base_lba = vol;
    nslots   = (disk - vol) / SECTORS_PER_PAGE;
    if (nslots > 65535) nslots = 65535;         /* slot numbers are 16-bit */
    bitmap   = kmalloc((nslots + 7) / 8);
    if (!bitmap) { nslots = 0; return; }
    memset(bitmap, 0, (nslots + 7) / 8);
    klog("[swap] %u pages past the volume, sectors %u to %u\n", nslots, base_lba, disk);
}

int      swap_available(void)   { return nslots != 0; }
uint32_t swap_slots_total(void) { return nslots; }
uint32_t swap_slots_used(void)  { return used; }

/* Lazily give a process its tables. */
static int tables(proc_t *pr) {
    if (pr->swap_slot) return 0;
    pr->swap_slot = kmalloc(MAX_HEAP_PAGES * sizeof(uint16_t));
    pr->fifo      = kmalloc(PROC_PAGE_CAP * sizeof(uint16_t));
    pr->last_ref  = kmalloc(MAX_HEAP_PAGES * sizeof(uint16_t));
    if (!pr->swap_slot || !pr->fifo || !pr->last_ref) {
        kfree(pr->swap_slot); kfree(pr->fifo); kfree(pr->last_ref);
        pr->swap_slot = 0; pr->fifo = 0; pr->last_ref = 0; return -1;
    }
    memset(pr->swap_slot, 0, MAX_HEAP_PAGES * sizeof(uint16_t));
    memset(pr->last_ref, 0, MAX_HEAP_PAGES * sizeof(uint16_t));
    pr->epoch = 0; pr->evicts_since_sample = 0;
    pr->fifo_head = pr->fifo_len = 0;
    return 0;
}

static uint32_t heap_index(uint32_t va) { return (va - USER_HEAP_BASE) >> 12; }

void swap_note_resident(proc_t *pr, uint32_t va) {
    if (!nslots || tables(pr) < 0) return;
    if (heap_index(va) >= MAX_HEAP_PAGES) return;
    if (pr->fifo_len >= PROC_PAGE_CAP) return;          /* cannot happen: cap bounds residents */
    pr->fifo[(pr->fifo_head + pr->fifo_len) % PROC_PAGE_CAP] = (uint16_t)heap_index(va);
    pr->fifo_len++;
    pr->last_ref[heap_index(va)] = pr->epoch;      /* arriving counts as a use */
}

/* The victim: approximately the least recently used resident page (v0.43.0).
 *
 * Every SAMPLE_EVERY evictions, each resident page's accessed bit is read and
 * cleared, and a page found used is stamped with the current epoch. The
 * victim is the resident page with the oldest stamp. FIFO, the first policy,
 * evicted fridge.elf's 512-page working set every round and fetched it all
 * back (3,584 page-ins). Clock, tried next, gave 8,152 second chances and the
 * same 3,584: the hot set is used in one phase and the sweep runs in another,
 * the hand laps the resident set twice per sweep, and a page cleared on one
 * lap is evicted on the next before it is used again. Clock approximates LRU
 * only within one revolution; this keeps the time of the last use itself. */
#define SAMPLE_EVERY 32

static void sample_accessed(proc_t *pr) {
    pr->epoch++;
    for (uint32_t k = 0; k < pr->fifo_len; k++) {
        uint16_t idx = pr->fifo[(pr->fifo_head + k) % PROC_PAGE_CAP];
        if (paging_test_and_clear_accessed(USER_HEAP_BASE + ((uint32_t)idx << 12)))
            pr->last_ref[idx] = pr->epoch;
    }
}

int swap_out_one(proc_t *pr) {
    if (!nslots || !pr->swap_slot) return -1;
    if (pr->evicts_since_sample++ % SAMPLE_EVERY == 0) sample_accessed(pr);

    /* Find the oldest stamp; drop entries for pages a shrinking sbrk freed. */
    int best = -1; uint16_t best_age = 0;
    for (uint32_t k = 0; k < pr->fifo_len; ) {
        uint32_t pos = (pr->fifo_head + k) % PROC_PAGE_CAP;
        uint16_t idx = pr->fifo[pos];
        if (!paging_is_mapped(USER_HEAP_BASE + ((uint32_t)idx << 12))) {
            uint32_t last = (pr->fifo_head + pr->fifo_len - 1) % PROC_PAGE_CAP;
            pr->fifo[pos] = pr->fifo[last];
            pr->fifo_len--;
            continue;
        }
        uint16_t age = (uint16_t)(pr->epoch - pr->last_ref[idx]);
        if (best < 0 || age > best_age) { best = (int)pos; best_age = age; }
        k++;
    }
    if (best < 0) return -1;
    uint16_t idx = pr->fifo[best];
    uint32_t last = (pr->fifo_head + pr->fifo_len - 1) % PROC_PAGE_CAP;
    pr->fifo[best] = pr->fifo[last];          /* the set has no order to keep */
    pr->fifo_len--;
    uint32_t va = USER_HEAP_BASE + ((uint32_t)idx << 12);

    int s = slot_alloc();
    if (s < 0) { klog("[swap] full: %u pages\n", nslots); return -1; }
    if (ata_write_sectors(base_lba + (uint32_t)s * SECTORS_PER_PAGE, SECTORS_PER_PAGE, (const uint8_t *)va) < 0) {
        slot_free((uint32_t)s); return -1;
    }
    uint32_t phys = paging_unmap(va);
    if (phys) pmm_free_page((void *)phys);
    pr->upages--;
    pr->swap_slot[idx] = (uint16_t)(s + 1);
    pr->swaps_out++;
    pr->swapped++;
    return 0;
}

int swap_in(proc_t *pr, uint32_t va) {
    if (!nslots || !pr->swap_slot) return 0;
    uint32_t idx = heap_index(va & ~0xFFFu);
    if (idx >= MAX_HEAP_PAGES || !pr->swap_slot[idx]) return 0;
    uint32_t s = (uint32_t)pr->swap_slot[idx] - 1;
    /* Room first: the cap counts residents, and this page is about to be one. */
    if (pr->upages >= PROC_PAGE_CAP && swap_out_one(pr) < 0) return -1;
    void *frame = pmm_alloc_page();
    if (!frame) return -1;
    uint32_t page = va & ~0xFFFu;
    paging_map(page, (uint32_t)frame, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
    if (ata_read_sectors(base_lba + s * SECTORS_PER_PAGE, SECTORS_PER_PAGE, (uint8_t *)page) < 0) {
        paging_unmap(page); pmm_free_page(frame); return -1;
    }
    pr->upages++;
    slot_free(s);
    pr->swap_slot[idx] = 0;
    pr->swaps_in++;
    pr->swapped--;
    swap_note_resident(pr, page);
    return 1;
}

void swap_release(proc_t *pr) {
    if (pr->swap_slot) {
        for (uint32_t i = 0; i < MAX_HEAP_PAGES; i++)
            if (pr->swap_slot[i]) slot_free((uint32_t)pr->swap_slot[i] - 1);
        kfree(pr->swap_slot);
    }
    kfree(pr->fifo);
    kfree(pr->last_ref);
    pr->swap_slot = 0; pr->fifo = 0; pr->last_ref = 0;
    pr->fifo_head = pr->fifo_len = 0;
    pr->swapped = 0;
}
