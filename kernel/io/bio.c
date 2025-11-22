#include "bio.h"
#include "heap.h"
#include "ide.h"
#include "string.h"
#include "terminal.h"

#define BIO_CACHE_SIZE 128

static buffer_head_t cache[BIO_CACHE_SIZE];
static buffer_head_t *lru_head = NULL;
static buffer_head_t *lru_tail = NULL;

void bio_init(void)
{
    printf("BIO: Init starting...\n");
    memset(cache, 0, sizeof(cache));
    for (int i = 0; i < BIO_CACHE_SIZE; i++)
    {
        cache[i].data = kmalloc(BIO_BLOCK_SIZE);
        if (!cache[i].data)
        {
            printf("BIO: kmalloc failed at index %d\n", i);
            return;
        }
        // Initialize as free/empty

        // Add to LRU list (initially all in list)
        if (lru_tail)
        {
            lru_tail->next = &cache[i];
            cache[i].prev = lru_tail;
            lru_tail = &cache[i];
        }
        else
        {
            lru_head = &cache[i];
            lru_tail = &cache[i];
        }
    }
    printf("Buffered I/O Initialized. Cache Size: %d blocks\n", BIO_CACHE_SIZE);
}

static void move_to_head(buffer_head_t *bh)
{
    if (bh == lru_head)
        return;

    // Detach
    if (bh->prev)
        bh->prev->next = bh->next;
    if (bh->next)
        bh->next->prev = bh->prev;
    if (bh == lru_tail)
        lru_tail = bh->prev;

    // Attach to head
    bh->next = lru_head;
    bh->prev = NULL;
    if (lru_head)
        lru_head->prev = bh;
    lru_head = bh;
    if (!lru_tail)
        lru_tail = bh;
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
    buffer_head_t *bh = lru_tail;
    while (bh)
    {
        if (bh->ref_count == 0)
        {
            // Found a victim
            if (bh->flags & BIO_FLAG_DIRTY)
            {
                // Write back if dirty
                ide_write_sectors(bh->device, bh->block, 1, bh->data);
                bh->flags &= ~BIO_FLAG_DIRTY;
            }

            bh->device = device;
            bh->block = block;
            bh->flags = 0; // Invalid
            bh->ref_count = 1;
            move_to_head(bh);
            return bh;
        }
        bh = bh->prev;
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
    bh->flags |= BIO_FLAG_DIRTY;
    // Immediate write-through for now to be safe, or write-back later
    // Let's do write-through for simplicity in this step, or rely on LRU eviction to write back.
    // But if we crash, data is lost.
    // Let's just mark dirty.
    // Actually, for stability, let's sync immediately.
    ide_write_sectors(bh->device, bh->block, 1, bh->data);
    bh->flags &= ~BIO_FLAG_DIRTY;
}

void brelse(buffer_head_t *bh)
{
    if (bh->ref_count > 0)
        bh->ref_count--;
}
