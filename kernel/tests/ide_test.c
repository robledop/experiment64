#include "test.h"
#include "ide.h"
#include "string.h"
#include "terminal.h" // For printf if needed

static bool ide_initialized = false;

TEST(test_ide_read_write)
{
    if (!ide_initialized)
    {
        ide_init();
        ide_initialized = true;
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
        printf("No IDE drive found! Skipping test.\n");
        return true; 
    }

    uint8_t write_buf[512];
    uint8_t read_buf[512];

    for (int i = 0; i < 512; i++)
    {
        write_buf[i] = (uint8_t)((i + 0xAA) & 0xFF);
    }

    // Use a sector that is likely safe.
    // Sector 2000 (1MB is 2048 sectors).
    // Let's use sector 20000 (10MB).
    uint32_t lba = 20000;

    // Write
    int res = ide_write_sectors(drive, lba, 1, write_buf);
    if (res != 0)
    {
        printf("IDE Write Failed\n");
        return false;
    }

    // Clear read buffer
    memset(read_buf, 0, 512);

    // Read back
    res = ide_read_sectors(drive, lba, 1, read_buf);
    if (res != 0)
    {
        printf("IDE Read Failed\n");
        return false;
    }

    // Verify
    if (memcmp(write_buf, read_buf, 512) != 0)
    {
        printf("IDE Read/Write Mismatch\n");
        return false;
    }

    return true;
}
