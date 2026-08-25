#pragma once
#include <stdint.h>

#define PAGE_PRESENT  0x01
#define PAGE_WRITE    0x02
#define PAGE_USER     0x04

void paging_init(void);
void paging_map(uint32_t virt, uint32_t phys, uint32_t flags);
uint32_t paging_unmap(uint32_t virt);   /* returns freed phys frame, or 0 */
