#include "bio.h"
#include "heap.h"
#include "storage.h"
#include "string.h"
#include "terminal.h"
#include "spinlock.h"

#define BIO_CACHE_SIZE 128

static buffer_head_t cache[BIO_CACHE_SIZE];
static LIST_HEAD(lru_list);
static spinlock_t bio_lock;
static bool bio_lock_initialized = false;

void bio_init(void)
{
    boot_message(INFO, "BIO: Init starting...");
    spinlock_init(&bio_lock);
    bio_lock_initialized = true;
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
        sleeplock_init(&cache[i].lock, "bio_buffer");

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

// Look for a cached buffer or allocate one.
// Called with bio_lock held, returns with bio_lock held.
// Returns a buffer with ref_count incremented but NOT locked.
static buffer_head_t *get_blk(uint8_t device, uint32_t block)
{
    // Search cache for existing buffer (regardless of validity)
    for (int i = 0; i < BIO_CACHE_SIZE; i++)
    {
        if (cache[i].ref_count > 0 && cache[i].device == device && cache[i].block == block)
        {
            cache[i].ref_count++;
            move_to_head(&cache[i]);
            return &cache[i];
        }
    }

    // Not found, recycle the LRU unused buffer
    buffer_head_t *bh;
    list_for_each_entry_reverse(bh, &lru_list, list)
    {
        if (bh->ref_count == 0)
        {
            // Write back dirty buffer before recycling
            if (bh->flags & BIO_FLAG_DIRTY)
            {
                storage_write(bh->device, bh->block, 1, bh->data);
                bh->flags &= ~BIO_FLAG_DIRTY;
            }
            bh->device = device;
            bh->block = block;
            bh->flags = 0; // Invalid - needs to be read
            bh->ref_count = 1;
            move_to_head(bh);
            return bh;
        }
    }

    printk("BIO: No free buffers!\n");
    return nullptr;
}

// Return a locked buffer with the contents of the indicated block.
buffer_head_t *bread(uint8_t device, uint32_t block)
{
    if (bio_lock_initialized)
        spinlock_acquire(&bio_lock);

    buffer_head_t *bh = get_blk(device, block);
    if (!bh)
    {
        if (bio_lock_initialized)
            spinlock_release(&bio_lock);
        return nullptr;
    }

    // Release spinlock before acquiring sleeplock (may sleep)
    if (bio_lock_initialized)
        spinlock_release(&bio_lock);

    // Acquire exclusive access to this buffer
    sleeplock_acquire(&bh->lock);

    // Check if we need to read from disk
    if (!(bh->flags & BIO_FLAG_VALID))
    {
        int rc = storage_read(device, block, 1, bh->data);
        if (rc != 0)
        {
            // Read failed - release buffer
            sleeplock_release(&bh->lock);
            if (bio_lock_initialized)
                spinlock_acquire(&bio_lock);
            bh->ref_count--;
            if (bio_lock_initialized)
                spinlock_release(&bio_lock);
            return nullptr;
        }
        bh->flags |= BIO_FLAG_VALID;
    }

    return bh; // Return with sleeplock held
}

// Write buffer contents to disk.
// Caller must hold the buffer lock.
void bwrite(buffer_head_t *bh)
{
    if (!bh)
        return;

    // Write to disk (we hold the sleeplock so data is stable)
    int rc = storage_write(bh->device, bh->block, 1, bh->data);
    if (rc != 0)
    {
        printk("BIO: Failed to write block %d\n", bh->block);
    }
    // Buffer is now clean (matches disk)
    bh->flags &= ~BIO_FLAG_DIRTY;
}

// Release a buffer - unlocks it and decrements ref_count.
void brelse(buffer_head_t *bh)
{
    if (!bh)
        return;

    // Release the sleeplock first
    sleeplock_release(&bh->lock);

    // Then update ref_count under spinlock
    if (bio_lock_initialized)
        spinlock_acquire(&bio_lock);

    if (bh->ref_count > 0)
        bh->ref_count--;

    if (bio_lock_initialized)
        spinlock_release(&bio_lock);
}
