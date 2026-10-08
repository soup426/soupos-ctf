#pragma once
#include <stdint.h>

int      ata_init(void);                                      /* 0=ok, -1=no drive */
int      ata_read_sector(uint32_t lba, uint8_t *buf);         /* 0=ok, -1=err      */
int      ata_write_sector(uint32_t lba, const uint8_t *buf);  /* 0=ok, -1=err      */
uint32_t ata_total_sectors(void);

/* Several consecutive sectors with ONE command: one settle and one BSY/DRQ
 * wait per request instead of per sector, which is where a sector's time
 * goes under a hypervisor. count is 1..255. For swap, which moves a page
 * (eight sectors) at a time. */
int      ata_read_sectors (uint32_t lba, uint8_t count, uint8_t *buf);
int      ata_write_sectors(uint32_t lba, uint8_t count, const uint8_t *buf);
