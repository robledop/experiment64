#include <ipc/shm.h>
#include <lib/string.h>
#include <lib/util.h>
#include <mem/heap.h>
#include <mem/pmm.h>
#include <mem/vmm.h>
#include <status.h>
#include <sys/fcntl.h>

// Shared memory implementation using a simple global table. This is not the most efficient design, but it is simple and
// works for a small number of shared memory objects.

static shm_entry_t *shm_table[SHM_MAX_OBJECTS];
static int shm_count;
static spinlock_t shm_lock;

void shm_init(void)
{
    spinlock_init(&shm_lock);
    shm_count = 0;
    memset(shm_table, 0, sizeof(shm_table));
}

shm_entry_t *shm_lookup(const char *name)
{
    for (int i = 0; i < shm_count; i++) {
        if (shm_table[i] && strcmp(shm_table[i]->name, name) == 0)
            return shm_table[i];
    }
    return nullptr;
}

/**
 * Create a new shared memory object with the given name and size. The size will be rounded up to a multiple of the page
 * size.
 * @param name The name of the shared memory object.
 * @param size The size of the shared memory object in bytes.
 * @return A pointer to the newly created shared memory object, or nullptr on failure.
 */
shm_entry_t *shm_create(const char *name, size_t size)
{
    if (!name || size == 0)
        return nullptr;
    if (strlen(name) >= SHM_NAME_MAX)
        return nullptr;
    if (shm_count >= SHM_MAX_OBJECTS)
        return nullptr;

    size_t num_pages = align_up(size, PAGE_SIZE) / PAGE_SIZE;

    shm_entry_t *entry = kmalloc(sizeof(shm_entry_t));
    if (!entry)
        return nullptr;

    entry->phys_pages = kmalloc(num_pages * sizeof(uint64_t));
    if (!entry->phys_pages) {
        kfree(entry);
        return nullptr;
    }

    for (size_t i = 0; i < num_pages; i++) {
        void *page = pmm_alloc_page();
        if (!page) {
            for (size_t j = 0; j < i; j++)
                pmm_free_page((void *)entry->phys_pages[j]);
            kfree(entry->phys_pages);
            kfree(entry);
            return nullptr;
        }
        memset((void *)((uint64_t)page + g_hhdm_offset), 0, PAGE_SIZE);
        entry->phys_pages[i] = (uint64_t)page;
    }

    strcpy(entry->name, name);
    entry->size              = size;
    entry->num_pages         = num_pages;
    entry->refcount          = 0;
    entry->marked_for_unlink = false;

    shm_table[shm_count++] = entry;
    return entry;
}

/**
 * Atomically look up (or create) an SHM entry and acquire a reference.
 * Holds shm_lock across lookup+ref so a concurrent shm_destroy cannot
 * free the entry between the two operations.
 * @return The entry with refcount incremented, or nullptr on failure.
 */
shm_entry_t *shm_open_or_create(const char *name, int flags, size_t size)
{
    bool create = (flags & O_CREAT) != 0;

    spinlock_acquire(&shm_lock);
    shm_entry_t *entry = shm_lookup(name);

    if (!entry && !create) {
        spinlock_release(&shm_lock);
        return nullptr;
    }

    if (!entry) {
        if (size == 0) {
            spinlock_release(&shm_lock);
            return nullptr;
        }
        entry = shm_create(name, size);
        if (!entry) {
            spinlock_release(&shm_lock);
            return nullptr;
        }
    }

    __atomic_add_fetch(&entry->refcount, 1, __ATOMIC_RELAXED);
    spinlock_release(&shm_lock);
    return entry;
}

void shm_ref(shm_entry_t *entry)
{
    if (entry)
        __atomic_add_fetch(&entry->refcount, 1, __ATOMIC_RELAXED);
}

static void shm_destroy(shm_entry_t *entry)
{
    for (size_t i = 0; i < entry->num_pages; i++)
        pmm_free_page((void *)entry->phys_pages[i]);
    kfree(entry->phys_pages);
    kfree(entry);
}

static void shm_remove_from_table(shm_entry_t *entry)
{
    for (int i = 0; i < shm_count; i++) {
        if (shm_table[i] == entry) {
            shm_table[i]             = shm_table[shm_count - 1];
            shm_table[shm_count - 1] = nullptr;
            shm_count--;
            return;
        }
    }
}

/**
 * Decrement the reference count of the shared memory object and destroy it if the count reaches zero, and it is marked
 * for unlinking.
 * @param entry The shared memory object to decrement the reference count of.
 */
void shm_unref(shm_entry_t *entry)
{
    if (!entry)
        return;

    int prev = __atomic_sub_fetch(&entry->refcount, 1, __ATOMIC_RELEASE);
    if (prev > 0)
        return;

    __atomic_thread_fence(__ATOMIC_ACQUIRE);

    spinlock_acquire(&shm_lock);
    if (entry->marked_for_unlink && entry->refcount <= 0) {
        shm_remove_from_table(entry);
        spinlock_release(&shm_lock);
        shm_destroy(entry);
        return;
    }
    spinlock_release(&shm_lock);
}

/**
 * Mark the shared memory object with the given name for unlinking. If the reference count of the object is zero, it
 * will be removed from the table and destroyed immediately. Otherwise, it will be removed and destroyed when the
 * reference count reaches zero.
 * @param name The name of the shared memory object to unlink.
 * @return 0 on success, -EINVAL if the name is invalid, or -ENOENT if the object does not exist.
 */
int shm_do_unlink(const char *name)
{
    if (!name)
        return -EINVAL;

    spinlock_acquire(&shm_lock);
    shm_entry_t *entry = shm_lookup(name);
    if (!entry) {
        spinlock_release(&shm_lock);
        return -ENOENT;
    }

    entry->marked_for_unlink = true;

    if (entry->refcount <= 0) {
        shm_remove_from_table(entry);
        spinlock_release(&shm_lock);
        shm_destroy(entry);
        return 0;
    }

    spinlock_release(&shm_lock);
    return 0;
}

static void shm_inode_close(vfs_inode_t *node)
{
    if (!node || !node->device)
        return;
    shm_unref((shm_entry_t *)node->device);
}

struct inode_operations shm_inode_ops = {
    .close = shm_inode_close,
};

bool shm_is_shm_inode(const vfs_inode_t *inode)
{
    return inode && inode->iops == &shm_inode_ops;
}
