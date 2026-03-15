#include <tests/test.h>
#include <drivers/ide.h>
#include <lib/string.h>
#include <drivers/terminal.h>

static bool ide_initialized = false;

TEST(test_ide_read_write)
{
    // IDE is already initialized during boot.
    // Avoid re-initializing unless needed.
    if (!ide_initialized)
    {
        for (int i = 0; i < 4; i++)
        {
            if (ide_devices[i].exists)
            {
                ide_initialized = true;
                break;
            }
        }
        if (!ide_initialized)
        {
            ide_init();
            ide_initialized = true;
        }
    }

    // Find a valid drive
    int drive = -1;
    for (int i = 0; i < 4; i++)
    {
        if (ide_devices[i].exists)
        {
            drive = i;
            break;
        }
    }

    if (drive == -1)
    {
        printk("No IDE drive found\n");
        return false;
    }

    uint8_t original_buf[512];
    uint8_t write_buf[512];
    uint8_t read_buf[512];
    bool restore_needed = false;
    bool passed = false;

    for (int i = 0; i < 512; i++)
    {
        write_buf[i] = (uint8_t)((i + 0xAA) & 0xFF);
    }

    // Use a sector that is likely safe.
    constexpr uint32_t lba = 20000;

    int res = ide_read_sectors(drive, lba, 1, original_buf);
    if (res != 0)
    {
        printk("IDE Initial Read Failed\n");
        return false;
    }

    res = ide_write_sectors(drive, lba, 1, write_buf);
    if (res != 0)
    {
        printk("IDE Write Failed\n");
        return false;
    }
    restore_needed = true;

    memset(read_buf, 0, 512);

    // Read back
    res = ide_read_sectors(drive, lba, 1, read_buf);
    if (res != 0)
    {
        printk("IDE Read Failed\n");
        goto out;
    }

    // Verify
    if (memcmp(write_buf, read_buf, 512) != 0)
    {
        printk("IDE Read/Write Mismatch\n");
        goto out;
    }

    passed = true;

out:
    if (restore_needed && ide_write_sectors(drive, lba, 1, original_buf) != 0)
    {
        printk("IDE Restore Failed\n");
        passed = false;
    }

    return passed;
}
