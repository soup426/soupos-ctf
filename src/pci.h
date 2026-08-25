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
const char *pci_vendor_str(uint16_t vendor_id);
const char *pci_class_str(uint8_t class_code);
