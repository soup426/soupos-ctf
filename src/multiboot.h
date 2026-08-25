#pragma once
#include <stdint.h>

/* Multiboot info structure passed by GRUB in EBX */
typedef struct {
    uint32_t flags;
    uint32_t mem_lower;       /* KB of lower memory  (if flags[0]) */
    uint32_t mem_upper;       /* KB of upper memory  (if flags[0]) */
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;     /* bytes in mmap table (if flags[6]) */
    uint32_t mmap_addr;       /* physical addr of mmap (if flags[6]) */
} __attribute__((packed)) multiboot_info_t;

/* Memory map entry - `size` does NOT include itself */
typedef struct {
    uint32_t size;
    uint32_t addr_low;
    uint32_t addr_high;
    uint32_t len_low;
    uint32_t len_high;
    uint32_t type;
} __attribute__((packed)) multiboot_mmap_t;

#define MULTIBOOT_MEMORY_AVAILABLE 1
