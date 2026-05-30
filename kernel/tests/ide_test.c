#include <tests/test.h>
#include <drivers/ide.h>
#include <lib/string.h>
#include <drivers/terminal.h>

static bool ide_initialized = false;

TEST(test_ide_parse_identify_offsets)
{
    // ATA IDENTIFY fields are addressed by word index; the byte offset is the
    // word index times two. Place sentinels at the correct word positions and
    // confirm the parser reads them (not the bytes at the raw word indices).
    uint8_t id[512];
    memset(id, 0, sizeof(id));

    id[0]   = 0x34;  // word 0  -> signature 0x1234
    id[1]   = 0x12;
    id[98]  = 0xBE;  // word 49 -> capabilities 0xCABE
    id[99]  = 0xCA;
    id[120] = 0x00;  // words 60-61 -> size 0x00DEAD00
    id[121] = 0xAD;
    id[122] = 0xDE;
    id[123] = 0x00;
    id[164] = 0xDE;  // words 82-83 -> command_sets 0x0000C0DE
    id[165] = 0xC0;

    ide_device_t dev;
    memset(&dev, 0, sizeof(dev));
    ide_parse_identify(id, &dev);

    TEST_ASSERT(dev.signature == 0x1234);
    TEST_ASSERT(dev.capabilities == 0xCABE);
    TEST_ASSERT(dev.size == 0x00DEAD00);
    TEST_ASSERT(dev.command_sets == 0x0000C0DE);
    return true;
}

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
