#pragma once

#include <drivers/pci.h>

void xhci_init(struct pci_device device);
bool xhci_usb_storage_present(void);
int xhci_usb_storage_read(uint32_t lba, uint8_t count, uint8_t *buffer);
int xhci_usb_storage_write(uint32_t lba, uint8_t count, const uint8_t *buffer);
