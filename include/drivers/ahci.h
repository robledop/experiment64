#pragma once

#include <drivers/pci.h>
#include <stdint.h>

void ahci_init(struct pci_device device);
bool ahci_port_ready(void);
int ahci_read(uint64_t lba, uint32_t sector_count, void *buffer);
int ahci_write(uint64_t lba, uint32_t sector_count, const void *buffer);
