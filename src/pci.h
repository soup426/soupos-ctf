#pragma once
#include <stdint.h>

#define PCI_MAX_DEVS 64

typedef struct {
    uint8_t  bus;
    uint8_t  dev;
    uint8_t  fn;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
} pci_dev_t;

int         pci_scan(pci_dev_t *out, int max);

/* Locate one device by vendor/device id. Returns 0 and fills *out on success,
 * -1 if it is not present. */
int         pci_find(uint16_t vendor, uint16_t device, pci_dev_t *out);

/* Configuration space. Offsets are the usual PCI ones (0x10 = BAR0,
 * 0x04 = command/status, 0x3C = interrupt line). */
uint32_t    pci_cfg_read32 (const pci_dev_t *d, uint8_t off);
void        pci_cfg_write32(const pci_dev_t *d, uint8_t off, uint32_t val);

/* BAR n with the type bits masked off: an I/O port base for I/O BARs, a
 * physical address for memory BARs. */
uint32_t    pci_bar(const pci_dev_t *d, int n);
uint8_t     pci_irq_line(const pci_dev_t *d);

/* Set the bus-master bit. A device that DMAs into our memory does nothing at
 * all without it, silently. */
void        pci_enable_bus_master(const pci_dev_t *d);
const char *pci_vendor_str(uint16_t vendor_id);
const char *pci_class_str(uint8_t class_code);
