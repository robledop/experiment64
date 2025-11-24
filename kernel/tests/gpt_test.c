#include "test.h"
#include "gpt.h"
#include "terminal.h"

static void partition_callback(partition_info_t *part)
{
    printk("  Partition: Start LBA: %lu, End LBA: %lu, Name: %s, Type: %s\n",
           part->start_lba, part->end_lba, part->name, gpt_get_guid_name(part->type_guid));
}

TEST(test_gpt_enumeration)
{
    printk("Enumerating partitions on Drive 0:\n");
    gpt_read_partitions(0, partition_callback);
    printk("GPT TEST COMPLETE\n");
    return true;
}
