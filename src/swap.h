#pragma once
#include <stdint.h>
#include "proc.h"

/* Swap: memory the machine does not have, kept on disk. Fourth queue, item 1.
 *
 * The swap area is the part of the disk image past the FAT volume: the
 * Makefile makes a 48 MB image and formats the first 32 MB, and the 16 MB
 * after it, 4096 pages, is addressed here by slot (eight sectors each) with
 * no on-disk format at all. A disk with nothing past the volume has no swap,
 * and processes die at the page cap as they did in v0.33.0.
 *
 * Per process: a FIFO of its resident heap pages (oldest mapped first is the
 * victim; the accessed bit can come later) and a table from heap page to
 * swap slot. Page-out writes the page straight from its user address, unmaps
 * it and frees the frame; page-in maps a fresh frame and reads into it. Both
 * run inside the page-fault handler, in the faulting task's context, with
 * only the ATA mutex taken, which nothing holds while touching user memory.
 *
 * Only the per-process cap triggers eviction here. Global pressure (the PMM
 * itself empty) still fails the map; that is the next step, not this one. */

void     swap_init(void);                /* after fat_init; logs what it found */
int      swap_available(void);
uint32_t swap_slots_total(void);
uint32_t swap_slots_used(void);

/* The heap page at `va` (page-aligned, inside [USER_HEAP, brk)) was just
 * mapped for `pr`: remember it as the newest resident page. */
void     swap_note_resident(proc_t *pr, uint32_t va);

/* Make room: write pr's oldest resident heap page out and unmap it. 0 on
 * success, -1 if there is no swap, no slot, or nothing to evict. */
int      swap_out_one(proc_t *pr);

/* Is `va`'s page on disk for `pr`? If so bring it back: map a frame, read
 * the page, free the slot. 1 brought in, 0 not swapped, -1 failed. */
int      swap_in(proc_t *pr, uint32_t va);

/* The process is gone: free its slots and tables. */
void     swap_release(proc_t *pr);
