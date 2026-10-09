#include "pci.h"

/* PCI configuration space is accessed via two 32-bit I/O ports:
   0xCF8 - CONFIG_ADDRESS
   0xCFC - CONFIG_DATA

   Address format (32-bit):
     bit 31     : enable bit (must be 1)
     bits 23-16 : bus number
     bits 15-11 : device number (0-31)
     bits 10-8  : function number (0-7)
     bits 7-2   : register offset (dword-aligned)
     bits 1-0   : always 0 */

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint32_t inl(uint16_t port) {
    uint32_t r;
    __asm__ volatile ("inl %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}

static uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off) {
    uint32_t addr = (1u << 31)
                  | ((uint32_t)bus        << 16)
                  | ((uint32_t)(dev & 31) << 11)
                  | ((uint32_t)(fn  &  7) <<  8)
                  | (off & 0xFC);
    outl(0xCF8, addr);
    return inl(0xCFC);
}

int pci_scan(pci_dev_t *out, int max) {
    int count = 0;

    for (int bus = 0; bus < 256 && count < max; bus++) {
        for (int dev = 0; dev < 32 && count < max; dev++) {
            /* Fast check: if vendor == 0xFFFF, nothing here */
            uint32_t id0 = pci_read32((uint8_t)bus, (uint8_t)dev, 0, 0x00);
            if ((id0 & 0xFFFF) == 0xFFFF) continue;

            /* Check multi-function flag (bit 7 of header type, byte at offset 0x0E) */
            uint32_t hdr  = pci_read32((uint8_t)bus, (uint8_t)dev, 0, 0x0C);
            int      fns  = ((hdr >> 16) & 0x80) ? 8 : 1;

            for (int fn = 0; fn < fns && count < max; fn++) {
                uint32_t id  = pci_read32((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 0x00);
                if ((id & 0xFFFF) == 0xFFFF) continue;

                uint32_t cls = pci_read32((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 0x08);

                out[count].bus        = (uint8_t)bus;
                out[count].dev        = (uint8_t)dev;
                out[count].fn         = (uint8_t)fn;
                out[count].vendor_id  = (uint16_t)(id & 0xFFFF);
                out[count].device_id  = (uint16_t)(id >> 16);
                out[count].class_code = (uint8_t)(cls >> 24);
                out[count].subclass   = (uint8_t)(cls >> 16);
                out[count].prog_if    = (uint8_t)(cls >> 8);
                count++;
            }
        }
    }
    return count;
}

const char *pci_vendor_str(uint16_t v) {
    switch (v) {
        case 0x8086: return "Intel";
        case 0x1022: return "AMD";
        case 0x10DE: return "NVIDIA";
        case 0x10EC: return "Realtek";
        case 0x1234: return "QEMU/Bochs";
        case 0x1AF4: return "VirtIO";
        case 0x1B36: return "QEMU";
        case 0x1002: return "AMD/ATI";
        case 0x15AD: return "VMware";
        default:     return "Unknown";
    }
}

const char *pci_class_str(uint8_t c) {
    switch (c) {
        case 0x00: return "Unclassified";
        case 0x01: return "Storage";
        case 0x02: return "Network";
        case 0x03: return "Display";
        case 0x04: return "Multimedia";
        case 0x05: return "Memory";
        case 0x06: return "Bridge";
        case 0x07: return "Serial";
        case 0x08: return "System";
        case 0x09: return "Input";
        case 0x0A: return "Docking";
        case 0x0B: return "Processor";
        case 0x0C: return "USB/Serial";
        case 0x0D: return "Wireless";
        case 0x0F: return "Satellite";
        case 0xFF: return "Misc";
        default:   return "Unknown";
    }
}

/* ── Device lookup and configuration space ─────────────────────────────── */

static void pci_write32(uint8_t bus, uint8_t dev, uint8_t fn,
                        uint8_t off, uint32_t val) {
    uint32_t addr = (1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
                    ((uint32_t)fn << 8) | (off & 0xFC);
    outl(0xCF8, addr);
    outl(0xCFC, val);
}

int pci_find(uint16_t vendor, uint16_t device, pci_dev_t *out) {
    pci_dev_t devs[PCI_MAX_DEVS];
    int n = pci_scan(devs, PCI_MAX_DEVS);
    for (int i = 0; i < n; i++) {
        if (devs[i].vendor_id == vendor && devs[i].device_id == device) {
            if (out) *out = devs[i];
            return 0;
        }
    }
    return -1;
}

uint32_t pci_cfg_read32(const pci_dev_t *d, uint8_t off) {
    return pci_read32(d->bus, d->dev, d->fn, off);
}

void pci_cfg_write32(const pci_dev_t *d, uint8_t off, uint32_t val) {
    pci_write32(d->bus, d->dev, d->fn, off, val);
}

uint32_t pci_bar(const pci_dev_t *d, int n) {
    uint32_t v = pci_cfg_read32(d, (uint8_t)(0x10 + 4 * n));
    /* Bit 0 selects the space: 1 = I/O (low 2 bits are flags), 0 = memory
     * (low 4 bits are flags). */
    return (v & 1u) ? (v & ~0x3u) : (v & ~0xFu);
}

uint8_t pci_irq_line(const pci_dev_t *d) {
    return (uint8_t)(pci_cfg_read32(d, 0x3C) & 0xFF);
}

void pci_enable_bus_master(const pci_dev_t *d) {
    uint32_t cmd = pci_cfg_read32(d, 0x04);
    cmd |= (1u << 2) | (1u << 0);   /* bus master + I/O space */
    pci_cfg_write32(d, 0x04, cmd);
}
