#include <io/bio.h>
#include <drivers/terminal.h>
#include <tests/test.h>

TEST(bio_test)
{
    printk("BIO Test: Starting...\n");

    // Use a safe sector (not sector 0 which holds the protective MBR / GPT header).
    constexpr uint32_t test_sector = 1999;

    buffer_head_t *bh = bread(0, test_sector);
    if (!bh)
    {
        printk("BIO Test: Failed to read block %u\n", test_sector);
        return false;
    }

    // Save original data so we can restore it.
    uint8_t saved[2] = {bh->data[0], bh->data[1]};

    bh->data[0] = 0xAA;
    bh->data[1] = 0x55;
    bwrite(bh);
    brelse(bh);

    // Read the block again and verify the write persisted through the cache.
    buffer_head_t *bh2 = bread(0, test_sector);
    if (!bh2)
    {
        printk("BIO Test: Failed to read block %u again\n", test_sector);
        return false;
    }

    bool success = (bh2->data[0] == 0xAA && bh2->data[1] == 0x55);
    if (!success)
        printk("BIO Test: Data verification failed\n");

    // Restore original data.
    bh2->data[0] = saved[0];
    bh2->data[1] = saved[1];
    bwrite(bh2);
    brelse(bh2);

    if (!success)
        return false;

    printk("BIO Test: Starting Stress Test (Cache Exhaustion)...\n");

    // Stress Test: Read/Write more blocks than cache size (128)
    // We use 200 blocks starting at sector 2000 to avoid FS structures
    constexpr int stress_count = 200;
    constexpr int start_sector = 2000;

    // Write patterns
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

    // Verify patterns
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
