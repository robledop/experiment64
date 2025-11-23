#include "bio.h"
#include "heap.h"
#include "ide.h"
#include "string.h"
#include "terminal.h"

#define BIO_CACHE_SIZE 128

static buffer_head_t cache[BIO_CACHE_SIZE];
static LIST_HEAD(lru_list);

void bio_init(void)
{
    boot_message(INFO, "BIO: Init starting...");
    memset(cache, 0, sizeof(cache));
    for (int i = 0; i < BIO_CACHE_SIZE; i++)
    {
        cache[i].data = kmalloc(BIO_BLOCK_SIZE);
        if (!cache[i].data)
        {
            boot_message(ERROR, "BIO: kmalloc failed at index %d", i);
            return;
        }
        // Initialize as free/empty
        INIT_LIST_HEAD(&cache[i].list);

        // Add to LRU list (initially all in list)
        list_add_tail(&cache[i].list, &lru_list);
    }
    boot_message(INFO, "Buffered I/O Initialized. Cache Size: %d blocks", BIO_CACHE_SIZE);
}

static void move_to_head(buffer_head_t *bh)
{
    list_del(&bh->list);
    list_add(&bh->list, &lru_list);
}

static buffer_head_t *get_blk(uint8_t device, uint32_t block)
{
    // 1. Search cache
    for (int i = 0; i < BIO_CACHE_SIZE; i++)
    {
        if (cache[i].device == device && cache[i].block == block && (cache[i].flags & BIO_FLAG_VALID))
        {
            cache[i].ref_count++;
            move_to_head(&cache[i]);
            return &cache[i];
        }
    }

    // 2. Not found, find free or LRU
    // We pick from tail (LRU)
    buffer_head_t *bh;
    list_for_each_entry_reverse(bh, &lru_list, list)
    {
        if (bh->ref_count == 0)
        {
            // Found a victim
            if (bh->flags & BIO_FLAG_DIRTY)
            {
                // Write back if dirty
                if (ide_write_sectors(bh->device, bh->block, 1, bh->data) != 0)
                {
                    printf("BIO: Failed to write back block %d\n", bh->block);
                    // If write fails, we shouldn't reuse this buffer if we care about data integrity.
                    // But for now, we proceed to avoid deadlock, but data is lost.
                }
                bh->flags &= ~BIO_FLAG_DIRTY;
            }

            bh->device = device;
            bh->block = block;
            bh->flags = 0; // Invalid
            bh->ref_count = 1;
            move_to_head(bh);
            return bh;
        }
    }

    printf("BIO: No free buffers!\n");
    return NULL;
}

buffer_head_t *bread(uint8_t device, uint32_t block)
{
    buffer_head_t *bh = get_blk(device, block);
    if (!bh)
        return NULL;

    if (!(bh->flags & BIO_FLAG_VALID))
    {
        if (ide_read_sectors(device, block, 1, bh->data) != 0)
        {
            brelse(bh);
            return NULL;
        }
        bh->flags |= BIO_FLAG_VALID;
    }
    return bh;
}

void bwrite(buffer_head_t *bh)
{
    if (!bh)
        return;
    bh->flags |= BIO_FLAG_DIRTY;
    // For now, write-through
    if (ide_write_sectors(bh->device, bh->block, 1, bh->data) != 0)
    {
        // printf("BIO: bwrite failed for block %d\n", bh->block);
    }
    else
    {
        bh->flags &= ~BIO_FLAG_DIRTY;
    }
}

void brelse(buffer_head_t *bh)
{
    if (bh->ref_count > 0)
        bh->ref_count--;
}
