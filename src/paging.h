#pragma once
#include <stdint.h>

#define PAGE_PRESENT  0x01
#define PAGE_WRITE    0x02
#define PAGE_USER     0x04

/* Index of the first page-directory entry that belongs to user space.
 * soupOS identity-maps the kernel low and puts user space at 0xC0000000, so
 * PDEs 0..767 are kernel and 768..1023 are per-process. */
#define PAGING_USER_PDE  (0xC0000000u >> 22)

void paging_init(void);

/* ── Address spaces ───────────────────────────────────────────────────────
 * Every directory shares the kernel's page TABLES for PDEs below
 * PAGING_USER_PDE, so a kernel mapping made in one address space is visible
 * in all of them. Only the user half is private.
 *
 * paging_new_dir returns a 4 KB-aligned directory, or NULL. Because the
 * kernel is identity-mapped, the returned pointer is also its physical
 * address, which is what CR3 wants. */
uint32_t *paging_new_dir(void);
void      paging_free_dir(uint32_t *dir);   /* frees user tables + the dir */
void      paging_switch(uint32_t *dir);     /* load CR3 and retarget mapping */
uint32_t *paging_kernel_dir(void);
uint32_t *paging_current_dir(void);

/* 1 if virt has a present mapping in the current address space. */
int       paging_is_mapped(uint32_t virt);
void paging_map(uint32_t virt, uint32_t phys, uint32_t flags);
uint32_t paging_unmap(uint32_t virt);   /* returns freed phys frame, or 0 */
