#include "bio.h"
#include "terminal.h"
#include "string.h"
#include "test.h"

TEST(bio_test)
{
    printk("BIO Test: Starting...\n");

    // Test 1: Read a block
    buffer_head_t *bh = bread(0, 0);
    if (!bh)
    {
        printk("BIO Test: Failed to read block 0\n");
        return false;
    }
    printk("BIO Test: Read block 0 successfully\n");

    // Test 2: Modify the block and write it back
    bh->data[0] = 0xAA;
    bh->data[1] = 0x55;
    bwrite(bh);
    printk("BIO Test: Wrote to block 0\n");

    // Release the buffer before reading it again
    brelse(bh);

    // Test 3: Read the block again and verify the data is cached
    buffer_head_t *bh2 = bread(0, 0);
    if (!bh2)
    {
        printk("BIO Test: Failed to read block 0 again\n");
        return false;
    }

    bool success = false;
    if (bh2->data[0] == 0xAA && bh2->data[1] == 0x55)
    {
        printk("BIO Test: Data verification successful (cached)\n");
        success = true;
    }
    else
    {
        printk("BIO Test: Data verification failed\n");
    }

    brelse(bh2);

    if (!success)
        return false;

    printk("BIO Test: Starting Stress Test (Cache Exhaustion)...\n");

    // Stress Test: Read/Write more blocks than cache size (128)
    // We use 200 blocks starting at sector 2000 to avoid FS structures
    int stress_count = 200;
    int start_sector = 2000;

    // 1. Write patterns
    for (int i = 0; i < stress_count; i++)
    {
        buffer_head_t *sbh = bread(0, start_sector + i);
        if (!sbh)
        {
            printk("BIO Stress: Failed to read block %d\n", start_sector + i);
            return false;
        }

        // Write a unique pattern: sector number in first 4 bytes
        uint32_t *data = (uint32_t *)sbh->data;
        *data = 0xDEADBEEF + i;

        bwrite(sbh);
        brelse(sbh);

        if ((i + 1) % 50 == 0)
            printk("BIO Stress: Wrote %d blocks\n", i + 1);
    }

    // 2. Verify patterns
    printk("BIO Stress: Verifying...\n");
    for (int i = 0; i < stress_count; i++)
    {
        buffer_head_t *sbh = bread(0, start_sector + i);
        if (!sbh)
        {
            printk("BIO Stress: Failed to read block %d for verification\n", start_sector + i);
            return false;
        }

        uint32_t *data = (uint32_t *)sbh->data;
        if (*data != 0xDEADBEEF + i)
        {
            printk("BIO Stress: Verification failed at block %d. Expected %x, Got %x\n",
                   start_sector + i, 0xDEADBEEF + i, *data);
            brelse(sbh);
            return false;
        }
        brelse(sbh);
    }

    printk("BIO Stress: Completed successfully.\n");
    return true;
}
