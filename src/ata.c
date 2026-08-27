#include "ata.h"
#include "task.h"

/* ATA is reached only through fat.c today, and fat.c is already behind its
 * own lock, so this is defence in depth rather than a live fix. It is worth
 * having: the module then protects itself instead of depending on an
 * invariant about who calls it, which is exactly the kind of thing that
 * quietly stops being true. A mutex rather than preempt_disable because the
 * PIO wait loop yields.
 *
 * Lock order is fat_mtx -> ata_mtx, and ATA never calls back into FAT, so
 * there is no cycle. */
static mutex_t ata_mtx;

/* Primary ATA channel I/O ports */
#define ATA_DATA       0x1F0
#define ATA_SECCOUNT   0x1F2
#define ATA_LBA_LO     0x1F3
#define ATA_LBA_MID    0x1F4
#define ATA_LBA_HI     0x1F5
#define ATA_DRIVE_HEAD 0x1F6
#define ATA_STATUS     0x1F7
#define ATA_COMMAND    0x1F7
#define ATA_ALT_STATUS 0x3F6

#define ATA_SR_BSY  0x80
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01

#define ATA_CMD_READ_SECTORS 0x20
#define ATA_CMD_IDENTIFY     0xEC

static int      drive_present = 0;
static uint32_t total_sec     = 0;

static inline uint8_t inb(uint16_t port) {
    uint8_t r; __asm__ volatile ("inb %1,%0":"=a"(r):"Nd"(port)); return r;
}
static inline uint16_t inw(uint16_t port) {
    uint16_t r; __asm__ volatile ("inw %1,%0":"=a"(r):"Nd"(port)); return r;
}
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0,%1"::"a"(val),"Nd"(port));
}
static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0,%1"::"a"(val),"Nd"(port));
}

/* Read alt-status 4× ≈ 400 ns delay */
static void delay400(void) {
    inb(ATA_ALT_STATUS); inb(ATA_ALT_STATUS);
    inb(ATA_ALT_STATUS); inb(ATA_ALT_STATUS);
}

/* The PIO wait loops below yield the CPU periodically so a slow drive
 * doesn't starve the cooperative scheduler. task_yield() is a safe no-op
 * before task_init() (ata_init / fat_init run that early). Yielding only
 * happens while waiting for the drive to become ready - never mid-transfer
 * - and disk access is currently driven by a single task (the shell), so
 * no other task can issue an overlapping command in the yield window. */
static int wait_bsy(void) {
    int t = 2000000;
    while ((inb(ATA_STATUS) & ATA_SR_BSY) && --t) {
        if ((t & 0x3FFF) == 0) task_yield();
    }
    return t ? 0 : -1;
}

/* Returns 0 on DRQ, -1 on ERR or timeout */
static int wait_drq(void) {
    int t = 2000000;
    uint8_t s;
    while (--t) {
        s = inb(ATA_STATUS);
        if (s & ATA_SR_ERR) return -1;
        if (s & ATA_SR_DRQ) return 0;
        if ((t & 0x3FFF) == 0) task_yield();
    }
    return -1;
}

int ata_init(void) {
    drive_present = 0;

    /* Select master drive on primary channel */
    outb(ATA_DRIVE_HEAD, 0xA0);
    delay400();

    /* Floating bus -> no drive */
    if (inb(ATA_STATUS) == 0xFF) return -1;

    /* Zero out task file before IDENTIFY */
    outb(ATA_SECCOUNT, 0);
    outb(ATA_LBA_LO,   0);
    outb(ATA_LBA_MID,  0);
    outb(ATA_LBA_HI,   0);

    /* Send IDENTIFY */
    outb(ATA_COMMAND, ATA_CMD_IDENTIFY);
    delay400();

    if (inb(ATA_STATUS) == 0) return -1; /* No drive */
    if (wait_bsy() < 0)       return -1;

    /* ATAPI device: LBA mid/hi non-zero after IDENTIFY */
    if (inb(ATA_LBA_MID) || inb(ATA_LBA_HI)) return -1;

    if (wait_drq() < 0) return -1;

    /* Consume all 256 words of IDENTIFY data */
    uint16_t id[256];
    for (int i = 0; i < 256; i++)
        id[i] = inw(ATA_DATA);

    /* Word 49 bit 9: LBA addressing supported */
    if (!(id[49] & 0x0200)) return -1;

    /* Words 60-61: total 28-bit LBA sectors */
    total_sec = (uint32_t)id[60] | ((uint32_t)id[61] << 16);
    if (total_sec == 0) return -1;

    drive_present = 1;
    return 0;
}

static int ata_read_sector_impl(uint32_t lba, uint8_t *buf) {
    if (!drive_present) return -1;

    if (wait_bsy() < 0) return -1;

    outb(ATA_DRIVE_HEAD, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_SECCOUNT,   1);
    outb(ATA_LBA_LO,     (uint8_t) lba);
    outb(ATA_LBA_MID,    (uint8_t)(lba >>  8));
    outb(ATA_LBA_HI,     (uint8_t)(lba >> 16));
    outb(ATA_COMMAND,    ATA_CMD_READ_SECTORS);

    delay400();

    if (wait_bsy() < 0) return -1;
    if (wait_drq() < 0) return -1;

    for (int i = 0; i < 256; i++) {
        uint16_t w = inw(ATA_DATA);
        buf[i * 2]     = (uint8_t)w;
        buf[i * 2 + 1] = (uint8_t)(w >> 8);
    }
    return 0;
}

static int ata_write_sector_impl(uint32_t lba, const uint8_t *buf) {
    if (!drive_present) return -1;

    if (wait_bsy() < 0) return -1;

    outb(ATA_DRIVE_HEAD, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_SECCOUNT,   1);
    outb(ATA_LBA_LO,     (uint8_t) lba);
    outb(ATA_LBA_MID,    (uint8_t)(lba >>  8));
    outb(ATA_LBA_HI,     (uint8_t)(lba >> 16));
    outb(ATA_COMMAND,    0x30);  /* WRITE SECTORS */

    delay400();
    if (wait_bsy() < 0) return -1;
    if (wait_drq() < 0) return -1;

    for (int i = 0; i < 256; i++) {
        uint16_t w = (uint16_t)buf[i * 2] | ((uint16_t)buf[i * 2 + 1] << 8);
        outw(ATA_DATA, w);
    }

    /* Wait for completion then flush write cache */
    if (wait_bsy() < 0) return -1;
    outb(ATA_COMMAND, 0xE7);  /* CACHE FLUSH */
    if (wait_bsy() < 0) return -1;

    return 0;
}

uint32_t ata_total_sectors(void) { return total_sec; }

int ata_read_sector(uint32_t lba, uint8_t *buf) {
    mutex_lock(&ata_mtx);
    int _r = ata_read_sector_impl(lba, buf);
    mutex_unlock(&ata_mtx);
    return _r;
}

int ata_write_sector(uint32_t lba, const uint8_t *buf) {
    mutex_lock(&ata_mtx);
    int _r = ata_write_sector_impl(lba, buf);
    mutex_unlock(&ata_mtx);
    return _r;
}
