#include <io/bio.h>
#include <mem/heap.h>
#include <io/storage.h>
#include <lib/string.h>
#include <drivers/terminal.h>
#include <task/spinlock.h>

#define BIO_CACHE_SIZE 512

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
        list_init_head(&cache[i].list);
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
    spinlock_assert_held(&bio_lock);
    int retries = 0;
    constexpr int max_retries = 100;  // Prevent infinite loop

retry:
    // Search cache for existing buffer (must not be recycling)
    for (int i = 0; i < BIO_CACHE_SIZE; i++)
    {
        if (cache[i].ref_count > 0 &&
            !(cache[i].flags & BIO_FLAG_RECYCLING) &&
            cache[i].device == device &&
            cache[i].block == block)
        {
            cache[i].ref_count++;
            move_to_head(&cache[i]);
            return &cache[i];
        }
    }

    // Not found, recycle the LRU unused buffer
    buffer_head_t *bh;
    list_foreach_entry_reverse(bh, &lru_list, list)
    {
        if (bh->ref_count == 0)
        {
            // Mark as in-use immediately to prevent others from taking it
            bh->ref_count = 1;

            // If dirty, we need to write it back - but we can't hold spinlock during I/O
            if (bh->flags & BIO_FLAG_DIRTY)
            {
                // Save old device/block for the write-back
                uint8_t old_device = bh->device;
                uint32_t old_block = bh->block;

                // Mark as recycling - this prevents other CPUs from matching this buffer
                bh->flags = BIO_FLAG_RECYCLING;

                // Release spinlock, acquire buffer's sleeplock, write, release sleeplock, reacquire spinlock
                spinlock_release(&bio_lock);
                sleeplock_acquire(&bh->lock);

                // Write back the old data using saved device/block
                storage_write(old_device, old_block, 1, bh->data);

                sleeplock_release(&bh->lock);
                spinlock_acquire(&bio_lock);

                // After reacquiring lock, check if someone else created a buffer for our block
                // while we were writing. If so, release this buffer and use theirs.
                for (int i = 0; i < BIO_CACHE_SIZE; i++)
                {
                    if (&cache[i] != bh &&
                        cache[i].ref_count > 0 &&
                        !(cache[i].flags & BIO_FLAG_RECYCLING) &&
                        cache[i].device == device &&
                        cache[i].block == block)
                    {
                        // Someone else created the buffer we needed - release ours and use theirs
                        bh->ref_count = 0;
                        bh->flags = 0;  // No longer recycling
                        cache[i].ref_count++;
                        move_to_head(&cache[i]);
                        return &cache[i];
                    }
                }

                // No duplicate found, use this buffer
                bh->device = device;
                bh->block = block;
                bh->flags = 0;  // Clear recycling, not valid yet
                move_to_head(bh);
                return bh;
            }

            bh->device = device;
            bh->block = block;
            bh->flags = 0;
            move_to_head(bh);
            return bh;
        }
    }

    // No free buffers - all have ref_count > 0
    // This can happen under heavy load. Wait briefly and retry.
    if (retries++ < max_retries)
    {
        // Release lock and yield to let other CPUs release their buffers
        spinlock_release(&bio_lock);

        // Yield CPU time - this allows other threads to run and release buffers
        // Use a longer pause and potentially yield to scheduler
        for (volatile int i = 0; i < 10000; i++)
            __asm__ volatile("pause");

        spinlock_acquire(&bio_lock);
        goto retry;
    }

    printk("BIO: No free buffers after %d retries! (requesting dev=%d block=%d)\n", max_retries, device, block);

    // Debug: count how many buffers are in use
    int in_use = 0, recycling = 0;
    for (int i = 0; i < BIO_CACHE_SIZE; i++) {
        if (cache[i].ref_count > 0) in_use++;
        if (cache[i].flags & BIO_FLAG_RECYCLING) recycling++;
    }
    printk("BIO: %d buffers in use, %d recycling\n", in_use, recycling);

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
            printk("BIO: storage_read failed dev=%d block=%d rc=%d\n", device, block, rc);
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

    sleeplock_assert_held(&bh->lock);

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
    if (!bh) return;

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
