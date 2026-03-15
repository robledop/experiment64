#include <tests/test.h>
#include <drivers/gpt.h>
#include <drivers/terminal.h>

static int g_partition_count = 0;
static bool g_has_valid_lba = false;

static void partition_callback(const partition_info_t *part)
{
    g_partition_count++;
    if (part->start_lba < part->end_lba)
        g_has_valid_lba = true;
    printk("  Partition: Start LBA: %lu, End LBA: %lu, Name: %s, Type: %s\n",
           part->start_lba, part->end_lba, part->name, gpt_get_guid_name(part->type_guid));
}

TEST(test_gpt_enumeration)
{
    g_partition_count = 0;
    g_has_valid_lba = false;

    gpt_read_partitions(0, partition_callback);

    // The test disk must have at least one partition.
    TEST_ASSERT(g_partition_count > 0);
    // At least one partition must have a valid LBA range.
    TEST_ASSERT(g_has_valid_lba);
    return true;
}
