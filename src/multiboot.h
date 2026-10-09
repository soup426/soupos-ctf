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
    /* Everything below is only touched when its flag is set. The offsets are
     * fixed by the specification, so the unused fields have to be declared to
     * put the framebuffer ones at 88 and up. */
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
    uint64_t framebuffer_addr;    /* +88, if flags[12] */
    uint32_t framebuffer_pitch;   /* bytes per scanline, NOT width * bytes  */
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
} __attribute__((packed)) multiboot_info_t;

#define MULTIBOOT_FLAG_FRAMEBUFFER (1u << 12)

/* framebuffer_type */
#define MULTIBOOT_FB_INDEXED 0
#define MULTIBOOT_FB_RGB     1
#define MULTIBOOT_FB_EGA     2    /* ordinary text mode: 0xB8000, 80x25 */

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
