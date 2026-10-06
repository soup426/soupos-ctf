#pragma once
#include <stdint.h>

int      ata_init(void);                                      /* 0=ok, -1=no drive */
int      ata_read_sector(uint32_t lba, uint8_t *buf);         /* 0=ok, -1=err      */
int      ata_write_sector(uint32_t lba, const uint8_t *buf);  /* 0=ok, -1=err      */
uint32_t ata_total_sectors(void);
