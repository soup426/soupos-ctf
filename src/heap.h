#pragma once
#include <stdint.h>

void     heap_init(void);
void    *kmalloc(uint32_t size);
void     kfree(void *ptr);
void    *krealloc(void *ptr, uint32_t size);
void    *kcalloc(uint32_t n, uint32_t size);
uint32_t kmalloc_size(void *ptr);

uint32_t heap_used_bytes(void);
uint32_t heap_free_bytes(void);
uint32_t heap_total_bytes(void);
