#pragma once

#include <drivers/pci.h>

void xhci_init(struct pci_device device);
bool xhci_usb_storage_present(void);
int xhci_usb_storage_read(uint32_t lba, uint8_t count, uint8_t *buffer);
int xhci_usb_storage_write(uint32_t lba, uint8_t count, const uint8_t *buffer);
/**
 * @brief Check whether a USB HID mouse or tablet device has been initialized.
 *
 * @return true if a HID mouse/tablet is active on the xHCI controller.
 */
bool xhci_usb_mouse_present(void);
