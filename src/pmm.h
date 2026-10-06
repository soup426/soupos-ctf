#pragma once
#include <stdint.h>

void     pmm_init(uint32_t mb_info_addr);
void    *pmm_alloc_page(void);
void     pmm_free_page(void *page);

uint32_t pmm_total_pages(void);
uint32_t pmm_used_pages(void);
uint32_t pmm_free_pages(void);
uint32_t pmm_total_kb(void);
uint32_t pmm_used_kb(void);
uint32_t pmm_free_kb(void);
