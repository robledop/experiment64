#pragma once

#include <stdint.h>

// Logical block device abstraction used by BIO/VFS.
// device 0: prefer AHCI port if available, else first IDE drive.
// device 1: next available IDE drive (if any) to allow mixed AHCI/IDE setups.

void storage_init(void);
int storage_read(uint8_t device, uint32_t lba, uint8_t count, uint8_t *buffer);
int storage_write(uint8_t device, uint32_t lba, uint8_t count, const uint8_t *buffer);
