#pragma once

#include <stdint.h>

#define IDE_ATA 0x00
#define IDE_ATAPI 0x01

typedef struct
{
    uint8_t exists;
    uint8_t channel; // 0 (Primary) or 1 (Secondary)
    uint8_t drive;   // 0 (Master) or 1 (Slave)
    uint16_t type;   // 0: ATA, 1: ATAPI
    uint16_t signature;
    uint16_t capabilities;
    uint32_t command_sets;
    uint32_t size; // Size in sectors
    char model[41];
} ide_device_t;

extern ide_device_t ide_devices[4];

void ide_init(void);
int ide_read_sectors(uint8_t drive_index, uint32_t lba, uint8_t count, uint8_t *buffer);
int ide_write_sectors(uint8_t drive_index, uint32_t lba, uint8_t count, uint8_t *buffer);
void ide_irq_handler(uint8_t channel);
