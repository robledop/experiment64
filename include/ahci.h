#pragma once

#include <pci.h>
#include <stdint.h>
#include <stdbool.h>

// AHCI register layout definitions based on the AHCI specification, section 3.3.
struct ahci_port
{
    uint32_t clb;      // 0x00, command list base address
    uint32_t clbu;     // 0x04, command list base address upper 32 bits
    uint32_t fb;       // 0x08, FIS base address
    uint32_t fbu;      // 0x0C, FIS base address upper 32 bits
    uint32_t is;       // 0x10, interrupt status
    uint32_t ie;       // 0x14, interrupt enable
    uint32_t cmd;      // 0x18, command and status
    uint32_t reserved; // 0x1C
    uint32_t tfd;      // 0x20, task file data
    uint32_t sig;      // 0x24, signature
    uint32_t ssts;     // 0x28, SATA status (SCR0)
    uint32_t sctl;     // 0x2C, SATA control (SCR2)
    uint32_t serr;     // 0x30, SATA error (SCR1)
    uint32_t sact;     // 0x34, SATA active (SCR3)
    uint32_t ci;       // 0x38, command issue
    uint32_t sntf;     // 0x3C, SATA notification (SCR4)
    uint32_t fbs;      // 0x40, FIS-based switch control
    uint32_t devslp;   // 0x44, device sleep
    uint32_t reserved2[10];
    uint32_t vendor[4];
} __attribute__((packed));

struct ahci_memory
{
    uint32_t cap;     // 0x00, host capability
    uint32_t ghc;     // 0x04, global host control
    uint32_t is;      // 0x08, interrupt status
    uint32_t pi;      // 0x0C, ports implemented
    uint32_t vs;      // 0x10, version
    uint32_t ccc_ctl; // 0x14, command completion coalescing control
    uint32_t ccc_pts; // 0x18, command completion coalescing ports
    uint32_t em_loc;  // 0x1C, enclosure management location
    uint32_t em_ctl;  // 0x20, enclosure management control
    uint32_t cap2;    // 0x24, host capabilities extended
    uint32_t bohc;    // 0x28, BIOS/OS handoff control and status
    uint8_t reserved[0xA0 - 0x2C];
    uint8_t vendor[0x100 - 0xA0];
    struct ahci_port ports[32];
} __attribute__((packed));

void ahci_init(struct pci_device device);
bool ahci_port_ready(void);
int ahci_read(uint64_t lba, uint32_t sector_count, void *buffer);
int ahci_write(uint64_t lba, uint32_t sector_count, const void *buffer);

#define AHCI_SECTOR_SIZE 512u
