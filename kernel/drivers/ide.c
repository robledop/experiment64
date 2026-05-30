#include <drivers/ide.h>
#include <arch/x86_64/port_io.h>
#include <lib/string.h>
#include <arch/x86_64/apic.h>
#include <drivers/terminal.h>
#include <task/sleeplock.h>

#define IDE_BSY 0x80
#define IDE_DRDY 0x40
#define IDE_DF 0x20
#define IDE_DRQ 0x08
#define IDE_ERR 0x01

#define IDE_CMD_READ 0x20
#define IDE_CMD_WRITE 0x30
#define IDE_CMD_IDENTIFY 0xEC
#define IDE_CMD_FLUSH_CACHE 0xE7
#define IDE_CMD_FLUSH_CACHE_EXT 0xEA

ide_device_t ide_devices[4];

static uint16_t ide_channels[2] = {0x1F0, 0x170};
static uint16_t ide_control[2]  = {0x3F6, 0x376};

// Note: we use polling (DRQ/BSY) for PIO transfers to avoid SMP/IRQ routing issues.
// The IRQ handler remains to ACK the device interrupt by reading status.

// Serialize PIO operations per channel.
static sleeplock_t ide_channel_lock[2];
static bool ide_channel_lock_inited = false;

void ide_irq_handler(uint8_t channel)
{
    // Read status register to clear interrupt
    inb(ide_channels[channel] + 7);
}

static uint8_t ide_buf[2048] = {0};

static void ide_delay(uint8_t channel)
{
    // Reading the Alternate Status port 15 times introduces a 400ns delay
    // which is suggested by the ATA spec after changing drive selection.
    // https://wiki.osdev.org/ATA_PIO_Mode#400ns_delays
    for (int i = 0; i < 15; i++) {
        inb(ide_channels[channel] + 7);
    }
}

static void ide_swap_and_trim_model(char *dst, const uint8_t *src)
{
    for (int k = 0; k < 40; k += 2) {
        dst[k]     = (char)src[k + 1];
        dst[k + 1] = (char)src[k];
    }
    dst[40] = 0;
    for (int k = 39; k > 0; k--) {
        if (dst[k] == ' ')
            dst[k] = 0;
        else
            break;
    }
}

void ide_parse_identify(const uint8_t *id, ide_device_t *dev)
{
    // IDENTIFY positions are word indices; the byte offset is the index * 2.
    dev->signature    = *((const uint16_t *)(id + 0 * 2));
    dev->capabilities = *((const uint16_t *)(id + 49 * 2));
    dev->command_sets = *((const uint32_t *)(id + 82 * 2));
    dev->size         = *((const uint32_t *)(id + 60 * 2)); // Total sectors (LBA28)
    ide_swap_and_trim_model(dev->model, id + 27 * 2);
}

static void ide_log_devices(void)
{
    boot_message(INFO, "IDE Initialized.");
    for (int i = 0; i < 4; i++) {
        if (ide_devices[i].exists) {
            boot_message(INFO, "IDE Drive %d: %s - %d Sectors", i, ide_devices[i].model, ide_devices[i].size);
        }
    }
}

// Consolidated wait function - waits for BSY to clear and specified flag to set
static uint8_t ide_wait_flag(uint8_t channel, uint8_t flag)
{
    uint64_t timeout = 1000000;
    while (timeout > 0) {
        uint8_t status = inb(ide_channels[channel] + 7);
        if (status & IDE_ERR)
            return 1; // Error
        if (!(status & IDE_BSY) && ((flag == 0) || (status & flag)))
            break;
        timeout--;
    }
    return 0;
}

#define ide_wait_ready(channel) ide_wait_flag((channel), IDE_DRDY)
#define ide_wait_drq(channel) ide_wait_flag((channel), IDE_DRQ)
#define ide_wait_not_bsy(channel) ide_wait_flag((channel), 0)

void ide_init(void)
{
    memset(ide_devices, 0, sizeof(ide_devices));

    if (!ide_channel_lock_inited) {
        sleeplock_init(&ide_channel_lock[0], "ide0");
        sleeplock_init(&ide_channel_lock[1], "ide1");
        ide_channel_lock_inited = true;
    }

    for (int i = 0; i < 2; i++) {
        // Channels
        for (int j = 0; j < 2; j++) {
            // Drives
            const int idx = i * 2 + j;
            uint8_t err   = 0;
            uint8_t type  = IDE_ATA;

            // Select Drive
            outb(ide_channels[i] + 6, 0xA0 | (j << 4));
            ide_delay(i);

            // Send Identify Command
            outb(ide_channels[i] + 7, IDE_CMD_IDENTIFY);
            ide_delay(i);

            if (inb(ide_channels[i] + 7) == 0)
                continue; // Drive does not exist

            while (1) {
                uint8_t status = inb(ide_channels[i] + 7);
                if (status & IDE_ERR) {
                    err = 1;
                    break;
                }
                if (!(status & IDE_BSY) && (status & IDE_DRDY))
                    break;
            }

            if (err) {
                continue;
            }

            // Read Identification Data
            insw(ide_channels[i] + 0, (void *)ide_buf, 256);

            ide_devices[idx].exists  = 1;
            ide_devices[idx].type    = type;
            ide_devices[idx].channel = i;
            ide_devices[idx].drive   = j;
            ide_parse_identify(ide_buf, &ide_devices[idx]);
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
    uint8_t slave   = ide_devices[drive_index].drive;

    // Serialize operations on the channel, but keep interrupts enabled so IRQ handlers can run.
    sleeplock_acquire(&ide_channel_lock[channel]);

    if (ide_wait_ready(channel) != 0) {
        sleeplock_release(&ide_channel_lock[channel]);
        return 1;
    }

    // (Polling mode) no IRQ state needed.

    outb(ide_channels[channel] + 6, 0xE0 | (slave << 4) | ((lba >> 24) & 0x0F));
    outb(ide_channels[channel] + 1, 0x00);
    outb(ide_channels[channel] + 2, count);
    outb(ide_channels[channel] + 3, (uint8_t)lba);
    outb(ide_channels[channel] + 4, (uint8_t)(lba >> 8));
    outb(ide_channels[channel] + 5, (uint8_t)(lba >> 16));
    outb(ide_channels[channel] + 7, IDE_CMD_READ);

    for (int i = 0; i < count; i++) {
        // Poll DRQ instead of waiting for IRQ; avoids SMP/IRQ routing issues.
        if (ide_wait_drq(channel) != 0) {
            sleeplock_release(&ide_channel_lock[channel]);
            return 1;
        }
        insw(ide_channels[channel] + 0, (void *)(buffer + i * 512), 256);
    }

    sleeplock_release(&ide_channel_lock[channel]);
    return 0;
}

int ide_write_sectors(uint8_t drive_index, uint32_t lba, uint8_t count, uint8_t *buffer)
{
    if (drive_index >= 4 || !ide_devices[drive_index].exists)
        return 1;

    uint8_t channel = ide_devices[drive_index].channel;
    uint8_t slave   = ide_devices[drive_index].drive;

    sleeplock_acquire(&ide_channel_lock[channel]);

    if (ide_wait_ready(channel) != 0) {
        sleeplock_release(&ide_channel_lock[channel]);
        return 1;
    }

    // (Polling mode) no IRQ state needed.

    outb(ide_channels[channel] + 6, 0xE0 | (slave << 4) | ((lba >> 24) & 0x0F));
    outb(ide_channels[channel] + 1, 0x00);
    outb(ide_channels[channel] + 2, count);
    outb(ide_channels[channel] + 3, (uint8_t)lba);
    outb(ide_channels[channel] + 4, (uint8_t)(lba >> 8));
    outb(ide_channels[channel] + 5, (uint8_t)(lba >> 16));
    outb(ide_channels[channel] + 7, IDE_CMD_WRITE);

    for (int i = 0; i < count; i++) {
        if (ide_wait_drq(channel) != 0) {
            sleeplock_release(&ide_channel_lock[channel]);
            return 1;
        }

        outsw(ide_channels[channel] + 0, (void *)(buffer + i * 512), 256);

        // Wait for the device to finish the sector (BSY clear).
        if (ide_wait_not_bsy(channel) != 0) {
            sleeplock_release(&ide_channel_lock[channel]);
            return 1;
        }
    }

    sleeplock_release(&ide_channel_lock[channel]);
    return 0;
}

int ide_flush_cache(uint8_t drive_index)
{
    if (drive_index >= 4 || !ide_devices[drive_index].exists || ide_devices[drive_index].type != IDE_ATA)
        return -1;

    uint8_t channel = ide_devices[drive_index].channel;
    uint8_t slave   = ide_devices[drive_index].drive;

    sleeplock_acquire(&ide_channel_lock[channel]);

    if (ide_wait_ready(channel) != 0) {
        sleeplock_release(&ide_channel_lock[channel]);
        return -1;
    }

    outb(ide_channels[channel] + 6, 0xE0 | (slave << 4));
    ide_delay(channel);
    outb(ide_channels[channel] + 7, IDE_CMD_FLUSH_CACHE);

    if (ide_wait_not_bsy(channel) != 0) {
        sleeplock_release(&ide_channel_lock[channel]);
        return -1;
    }

    sleeplock_release(&ide_channel_lock[channel]);
    return 0;
}