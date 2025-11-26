#include "test.h"
#include "storage.h"
#include "string.h"
#include <stdint.h>

// GPT header signature.
static const char gpt_sig[8] = {'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T'};

TEST(test_storage_disk0_gpt_signature)
{
    uint8_t buf[512];
    TEST_ASSERT(storage_read(0, 1, 1, buf) == 0);
    TEST_ASSERT(memcmp(buf, gpt_sig, sizeof(gpt_sig)) == 0);
    return true;
}

TEST(test_storage_disk1_gpt_signature)
{
    uint8_t buf[512];
    TEST_ASSERT(storage_read(1, 1, 1, buf) == 0);
    TEST_ASSERT(memcmp(buf, gpt_sig, sizeof(gpt_sig)) == 0);
    return true;
}

TEST(test_storage_disk1_ext2_superblock)
{
    // Partition starts at LBA 2048; ext2 superblock at offset 1024 bytes -> LBA start + 2.
    const uint32_t part_start_lba = 2048;
    uint8_t sb[1024];
    TEST_ASSERT(storage_read(1, part_start_lba + 2, 2, sb) == 0);

    // ext2 magic at offset 0x38.
    uint16_t magic = (uint16_t)(sb[56] | (sb[57] << 8));
    TEST_ASSERT(magic == 0xEF53);

    // log block size at offset 0x18 should be 0 for 1KiB blocks in our image.
    uint32_t log_block_size = sb[24];
    TEST_ASSERT(log_block_size == 0);
    return true;
}
