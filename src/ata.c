#include "ata.h"
#include "task.h"
#include "pci.h"
#include "klog.h"
#include "str.h"

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
static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
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

static void ata_dma_probe(void);

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
    ata_dma_probe();
    return 0;
}

/* ── Bus-master DMA (v0.39.0) ──────────────────────────────────────────────
 * Measured under KVM: moving a sector with PIO is one VM exit per 16-bit
 * word, even as `rep insw`, so a 4 KB swap page cost ~5,200 cycles x 2,048
 * words, 85% of a swap run's time. With bus-master DMA the controller writes
 * guest memory itself and the CPU does a dozen port accesses per request.
 *
 * The PIIX IDE function (class 01:01) has the bus-master registers at BAR4:
 * +0 command (bit 0 start, bit 3 direction: 1 = device to memory), +2 status
 * (bit 0 active, bit 1 error, bit 2 interrupt; write 1 to clear), +4 the
 * physical address of the PRD table. Kernel memory is identity-mapped, so a
 * static buffer's address IS its physical address; the buffer is 4 KB and
 * 4 KB-aligned, so its one PRD entry cannot cross a 64 KB boundary. Requests
 * go through it as a bounce buffer: the memcpy is nothing next to an exit. */
#define DMA_MAX_SECTORS 8
static uint16_t bm_base;                                  /* 0: no DMA, use PIO */
static uint32_t prdt[2]                    __attribute__((aligned(8)));
static uint8_t  dma_buf[DMA_MAX_SECTORS * 512] __attribute__((aligned(4096)));

static void ata_dma_probe(void) {
    pci_dev_t devs[32];
    int n = pci_scan(devs, 32);
    for (int i = 0; i < n; i++) {
        if (devs[i].class_code != 0x01 || devs[i].subclass != 0x01) continue;
        uint32_t bar4 = pci_bar(&devs[i], 4);
        if (!bar4 || bar4 > 0xFFFF) continue;
        pci_enable_bus_master(&devs[i]);
        bm_base = (uint16_t)bar4;
        klog("[ata] bus-master DMA at io 0x%x\n", bm_base);
        return;
    }
    klog("[ata] no bus-master IDE: PIO only\n");
}

/* One DMA command for count (1..8) sectors at lba, through dma_buf. */
static int dma_xfer(uint32_t lba, uint8_t count, int to_disk) {
    uint8_t dir = to_disk ? 0x00 : 0x08;
    prdt[0] = (uint32_t)dma_buf;
    prdt[1] = 0x80000000u | ((uint32_t)count * 512);      /* end of table */
    outb(bm_base + 0, 0);                                 /* stopped */
    outl(bm_base + 4, (uint32_t)prdt);
    outb(bm_base + 2, 0x06);                              /* clear error + interrupt */
    outb(bm_base + 0, dir);
    if (wait_bsy() < 0) return -1;
    outb(ATA_DRIVE_HEAD, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_SECCOUNT,   count);
    outb(ATA_LBA_LO,     (uint8_t) lba);
    outb(ATA_LBA_MID,    (uint8_t)(lba >>  8));
    outb(ATA_LBA_HI,     (uint8_t)(lba >> 16));
    outb(ATA_COMMAND,    to_disk ? 0xCA : 0xC8);          /* WRITE DMA / READ DMA */
    outb(bm_base + 0, (uint8_t)(dir | 0x01));             /* go */
    /* Done when the engine has stopped AND the drive is not busy. */
    int t = 2000000, ok = 0;
    while (--t) {
        uint8_t bs = inb(bm_base + 2);
        if (bs & 0x02) break;                             /* bus error */
        if (!(bs & 0x01) && !(inb(ATA_ALT_STATUS) & ATA_SR_BSY)) { ok = 1; break; }
        if ((t & 0x3FFF) == 0) task_yield();
    }
    outb(bm_base + 0, 0);
    uint8_t st = inb(ATA_STATUS);                         /* also acknowledges the drive */
    outb(bm_base + 2, 0x06);
    if (!ok || (st & ATA_SR_ERR)) {
        klog("[ata] DMA %s failed at lba %u (status 0x%x)\n", to_disk ? "write" : "read", lba, st);
        return -1;
    }
    return 0;
}

static int dma_read(uint32_t lba, uint8_t count, uint8_t *buf) {
    while (count) {
        uint8_t n = count > DMA_MAX_SECTORS ? DMA_MAX_SECTORS : count;
        if (dma_xfer(lba, n, 0) < 0) return -1;
        memcpy(buf, dma_buf, (uint32_t)n * 512);
        buf += (uint32_t)n * 512; lba += n; count = (uint8_t)(count - n);
    }
    return 0;
}

static int dma_write(uint32_t lba, uint8_t count, const uint8_t *buf) {
    while (count) {
        uint8_t n = count > DMA_MAX_SECTORS ? DMA_MAX_SECTORS : count;
        memcpy(dma_buf, buf, (uint32_t)n * 512);
        if (dma_xfer(lba, n, 1) < 0) return -1;
        buf += (uint32_t)n * 512; lba += n; count = (uint8_t)(count - n);
    }
    if (wait_bsy() < 0) return -1;                        /* durability, as PIO does */
    outb(ATA_COMMAND, 0xE7);                              /* CACHE FLUSH */
    return wait_bsy();
}

static int ata_read_sector_impl(uint32_t lba, uint8_t *buf) {
    if (!drive_present) return -1;
    if (bm_base) return dma_read(lba, 1, buf);

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

    /* One string instruction for the whole sector. Under a hypervisor every
     * port access is a VM exit, and 256 of them per sector made a sector
     * cost about a millisecond; a `rep insw` is handled as a single one. */
    {
        uint8_t *dst = buf; uint32_t n = 256;
        __asm__ volatile ("rep insw" : "+D"(dst), "+c"(n) : "d"(ATA_DATA) : "memory");
    }
    return 0;
}

static int ata_write_sector_impl(uint32_t lba, const uint8_t *buf) {
    if (!drive_present) return -1;
    if (bm_base) return dma_write(lba, 1, buf);

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

    {
        const uint8_t *src = buf; uint32_t n = 256;
        __asm__ volatile ("rep outsw" : "+S"(src), "+c"(n) : "d"(ATA_DATA) : "memory");
    }

    /* Wait for completion then flush write cache */
    if (wait_bsy() < 0) return -1;
    outb(ATA_COMMAND, 0xE7);  /* CACHE FLUSH */
    if (wait_bsy() < 0) return -1;

    return 0;
}

uint32_t ata_total_sectors(void) { return total_sec; }

static int ata_read_sectors_impl(uint32_t lba, uint8_t count, uint8_t *buf) {
    if (!drive_present || count == 0) return -1;
    if (bm_base) return dma_read(lba, count, buf);
    if (wait_bsy() < 0) return -1;
    outb(ATA_DRIVE_HEAD, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_SECCOUNT,   count);
    outb(ATA_LBA_LO,     (uint8_t) lba);
    outb(ATA_LBA_MID,    (uint8_t)(lba >>  8));
    outb(ATA_LBA_HI,     (uint8_t)(lba >> 16));
    outb(ATA_COMMAND,    ATA_CMD_READ_SECTORS);
    delay400();
    for (uint8_t k = 0; k < count; k++) {
        if (wait_bsy() < 0) return -1;
        if (wait_drq() < 0) return -1;
        uint8_t *dst = buf + (uint32_t)k * 512; uint32_t n = 256;
        __asm__ volatile ("rep insw" : "+D"(dst), "+c"(n) : "d"(ATA_DATA) : "memory");
    }
    return 0;
}

static int ata_write_sectors_impl(uint32_t lba, uint8_t count, const uint8_t *buf) {
    if (!drive_present || count == 0) return -1;
    if (bm_base) return dma_write(lba, count, buf);
    if (wait_bsy() < 0) return -1;
    outb(ATA_DRIVE_HEAD, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_SECCOUNT,   count);
    outb(ATA_LBA_LO,     (uint8_t) lba);
    outb(ATA_LBA_MID,    (uint8_t)(lba >>  8));
    outb(ATA_LBA_HI,     (uint8_t)(lba >> 16));
    outb(ATA_COMMAND,    0x30);  /* WRITE SECTORS */
    delay400();
    for (uint8_t k = 0; k < count; k++) {
        if (wait_bsy() < 0) return -1;
        if (wait_drq() < 0) return -1;
        const uint8_t *src = buf + (uint32_t)k * 512; uint32_t n = 256;
        __asm__ volatile ("rep outsw" : "+S"(src), "+c"(n) : "d"(ATA_DATA) : "memory");
    }
    if (wait_bsy() < 0) return -1;
    outb(ATA_COMMAND, 0xE7);  /* CACHE FLUSH */
    if (wait_bsy() < 0) return -1;
    return 0;
}

int ata_read_sectors(uint32_t lba, uint8_t count, uint8_t *buf) {
    mutex_lock(&ata_mtx);
    int r = ata_read_sectors_impl(lba, count, buf);
    mutex_unlock(&ata_mtx);
    return r;
}

int ata_write_sectors(uint32_t lba, uint8_t count, const uint8_t *buf) {
    mutex_lock(&ata_mtx);
    int r = ata_write_sectors_impl(lba, count, buf);
    mutex_unlock(&ata_mtx);
    return r;
}

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
