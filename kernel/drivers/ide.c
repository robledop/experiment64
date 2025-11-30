#include <ide.h>
#include <io.h>
#include <stddef.h>
#include <string.h>
#include "apic.h"
#include "terminal.h"

#define IDE_BSY 0x80
#define IDE_DRDY 0x40
#define IDE_DF 0x20
#define IDE_DRQ 0x08
#define IDE_ERR 0x01

#define IDE_CMD_READ 0x20
#define IDE_CMD_WRITE 0x30
#define IDE_CMD_IDENTIFY 0xEC

ide_device_t ide_devices[4];

static uint16_t ide_channels[2] = {0x1F0, 0x170};
static uint16_t ide_control[2] = {0x3F6, 0x376};

static volatile int ide_irq_invoked[2] = {0, 0};

void ide_irq_handler(uint8_t channel)
{
    ide_irq_invoked[channel] = 1;
    // Read status register to clear interrupt
    inb(ide_channels[channel] + 7);
}

static int ide_wait_irq(uint8_t channel)
{
    uint64_t timeout = 1000000;
    while (!ide_irq_invoked[channel] && timeout > 0)
    {
        timeout--;
        __asm__ volatile("nop");
    }
    if (timeout == 0)
    {
        return 1;
    }
    ide_irq_invoked[channel] = 0;
    return 0;
}

static uint8_t ide_buf[2048] = {0};

static void ide_delay(uint8_t channel)
{
    // Reading the Alternate Status port 4 times introduces a 400ns delay
    // which is suggested by the ATA spec after changing drive selection.
    for (int i = 0; i < 4; i++)
        inb(ide_channels[channel] + 7);
}

static void ide_swap_and_trim_model(char *dst, const uint8_t *src)
{
    for (int k = 0; k < 40; k += 2)
    {
        dst[k] = (char)src[k + 1];
        dst[k + 1] = (char)src[k];
    }
    dst[40] = 0;
    for (int k = 39; k > 0; k--)
    {
        if (dst[k] == ' ')
            dst[k] = 0;
        else
            break;
    }
}

static void ide_log_devices(void)
{
    boot_message(INFO, "IDE Initialized.");
    for (int i = 0; i < 4; i++)
    {
        if (ide_devices[i].exists)
        {
            boot_message(INFO, "IDE Drive %d: %s - %d Sectors", i, ide_devices[i].model, ide_devices[i].size);
        }
    }
}

// Consolidated wait function - waits for BSY to clear and specified flag to set
static uint8_t ide_wait_flag(uint8_t channel, uint8_t flag)
{
    uint8_t status;
    uint64_t timeout = 1000000;
    while (timeout > 0)
    {
        status = inb(ide_channels[channel] + 7);
        if (status & IDE_ERR)
            return 1; // Error
        if (!(status & IDE_BSY) && (status & flag))
            break;
        timeout--;
    }
    return 0;
}

#define ide_wait_ready(channel) ide_wait_flag((channel), IDE_DRDY)
#define ide_wait_drq(channel) ide_wait_flag((channel), IDE_DRQ)

void ide_init(void)
{
    memset(ide_devices, 0, sizeof(ide_devices));

    for (int i = 0; i < 2; i++)
    { // Channels
        for (int j = 0; j < 2; j++)
        { // Drives
            const int idx = i * 2 + j;
            uint8_t err = 0;
            uint8_t type = IDE_ATA;

            // Select Drive
            outb(ide_channels[i] + 6, 0xA0 | (j << 4));
            ide_delay(i);

            // Send Identify Command
            outb(ide_channels[i] + 7, IDE_CMD_IDENTIFY);
            ide_delay(i);

            if (inb(ide_channels[i] + 7) == 0)
                continue; // Drive does not exist

            while (1)
            {
                uint8_t status = inb(ide_channels[i] + 7);
                if (status & IDE_ERR)
                {
                    err = 1;
                    break;
                }
                if (!(status & IDE_BSY) && (status & IDE_DRDY))
                    break;
            }

            if (err)
            {
                // Try ATAPI (not implemented fully here, just skipping)
                continue;
            }

            // Read Identification Data
            insw(ide_channels[i] + 0, (void *)ide_buf, 256);

            ide_devices[idx].exists = 1;
            ide_devices[idx].type = type;
            ide_devices[idx].channel = i;
            ide_devices[idx].drive = j;
            ide_devices[idx].signature = *((uint16_t *)(ide_buf + 0));
            ide_devices[idx].capabilities = *((uint16_t *)(ide_buf + 49));
            ide_devices[idx].command_sets = *((uint32_t *)(ide_buf + 82));
            ide_devices[idx].size = *((uint32_t *)(ide_buf + 60)); // Total sectors (LBA28)

            // Model string needs byte swapping
            ide_swap_and_trim_model(ide_devices[idx].model, ide_buf + 27 * 2);
        }
    }
    ide_log_devices();

    // Enable IRQs
    outb(ide_control[0], 0);
    outb(ide_control[1], 0);

    apic_enable_irq(14, 46);
    apic_enable_irq(15, 47);
}

int ide_read_sectors(uint8_t drive_index, uint32_t lba, uint8_t count, uint8_t *buffer)
{
    if (drive_index >= 4 || !ide_devices[drive_index].exists)
        return 1;

    uint8_t channel = ide_devices[drive_index].channel;
    uint8_t slave = ide_devices[drive_index].drive;

    if (ide_wait_ready(channel) != 0)
        return 1; // Wait for BSY to clear before sending command

    // Clear IRQ flag before command
    ide_irq_invoked[channel] = 0;

    outb(ide_channels[channel] + 6, 0xE0 | (slave << 4) | ((lba >> 24) & 0x0F));
    outb(ide_channels[channel] + 1, 0x00);
    outb(ide_channels[channel] + 2, count);
    outb(ide_channels[channel] + 3, (uint8_t)lba);
    outb(ide_channels[channel] + 4, (uint8_t)(lba >> 8));
    outb(ide_channels[channel] + 5, (uint8_t)(lba >> 16));
    outb(ide_channels[channel] + 7, IDE_CMD_READ);

    for (int i = 0; i < count; i++)
    {
        if (ide_wait_irq(channel) != 0)
            return 1;
        if (ide_wait_drq(channel) != 0)
            return 1;
        insw(ide_channels[channel] + 0, (void *)(buffer + i * 512), 256);
    }

    return 0;
}

int ide_write_sectors(uint8_t drive_index, uint32_t lba, uint8_t count, uint8_t *buffer)
{
    if (drive_index >= 4 || !ide_devices[drive_index].exists)
        return 1;

    uint8_t channel = ide_devices[drive_index].channel;
    uint8_t slave = ide_devices[drive_index].drive;

    if (ide_wait_ready(channel) != 0)
        return 1;

    // Clear IRQ flag before command
    ide_irq_invoked[channel] = 0;

    outb(ide_channels[channel] + 6, 0xE0 | (slave << 4) | ((lba >> 24) & 0x0F));
    outb(ide_channels[channel] + 1, 0x00);
    outb(ide_channels[channel] + 2, count);
    outb(ide_channels[channel] + 3, (uint8_t)lba);
    outb(ide_channels[channel] + 4, (uint8_t)(lba >> 8));
    outb(ide_channels[channel] + 5, (uint8_t)(lba >> 16));
    outb(ide_channels[channel] + 7, IDE_CMD_WRITE);

    for (int i = 0; i < count; i++)
    {
        // Wait for DRQ
        if (ide_wait_drq(channel) != 0)
            return 1;

        outsw(ide_channels[channel] + 0, (void *)(buffer + i * 512), 256);

        if (ide_wait_irq(channel) != 0)
            return 1;
    }

    return 0;
}
