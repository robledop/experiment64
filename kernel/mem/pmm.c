#include "pmm.h"
#include "limine.h"
#include "string.h"
#include "terminal.h"
#include <stdbool.h>
#include <stdint.h>

#include "heap.h"
#include "debug.h"
#include "spinlock.h"

__attribute__((used, section(".requests"))) static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0};

static uint8_t *bitmap = nullptr;
static size_t bitmap_size = 0;
static size_t highest_page = 0;
static uint64_t highest_addr = 0;
static uint64_t pmm_hhdm_offset = 0;
static size_t reserved_base_page = 0;
static spinlock_t pmm_lock;

static int bitmap_test(size_t bit);

static void bitmap_set(size_t bit)
{
    bitmap[bit / 8] |= (1 << (bit % 8));
}

static void bitmap_unset(size_t bit)
{
    bitmap[bit / 8] &= ~(1 << (bit % 8));
}

static int bitmap_test(size_t bit)
{
    return bitmap[bit / 8] & (1 << (bit % 8));
}

void pmm_init(uint64_t hhdm_offset)
{
    spinlock_init(&pmm_lock);

    if (memmap_request.response == nullptr)
    {
        panic("Error: Limine memmap request failed");
    }

    struct limine_memmap_response *memmap = memmap_request.response;
    // Find the highest usable physical address
    for (uint64_t i = 0; i < memmap->entry_count; i++)
    {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE)
        {
            uint64_t top = entry->base + entry->length;
            if (top > highest_addr)
            {
                highest_addr = top;
            }
        }
    }

    highest_page = highest_addr / PAGE_SIZE;
    bitmap_size = highest_page / 8 + 1;
    pmm_hhdm_offset = hhdm_offset;

    // Find a place to put the bitmap
    for (uint64_t i = 0; i < memmap->entry_count; i++)
    {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE)
        {
            if (entry->length >= bitmap_size)
            {
                bitmap = (uint8_t *)(entry->base + hhdm_offset);
                // Initialize bitmap to all 1s (used)
                memset(bitmap, 0xFF, bitmap_size);
                break;
            }
        }
    }

    if (bitmap == nullptr)
    {
        panic("Error: Could not find memory for PMM bitmap");
    }

    // Mark usable regions as free (0) in the bitmap
    for (uint64_t i = 0; i < memmap->entry_count; i++)
    {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE)
        {
            for (uint64_t j = 0; j < entry->length; j += PAGE_SIZE)
            {
                bitmap_unset((entry->base + j) / PAGE_SIZE);
            }
        }
    }

    // Mark the bitmap itself as used
    // Note: bitmap pointer is virtual (HHDM), we need physical address for page calculation
    uint64_t bitmap_phys = (uint64_t)bitmap - hhdm_offset;
    uint64_t bitmap_start_page = bitmap_phys / PAGE_SIZE;
    uint64_t bitmap_pages = (bitmap_size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t i = 0; i < bitmap_pages; i++)
    {
        bitmap_set(bitmap_start_page + i);
    }

    reserved_base_page = bitmap_start_page + bitmap_pages + 16; // leave a guard region after the bitmap

    boot_message(INFO, "PMM bitmap phys=0x%lx virt=%p size=%zu pages=%lu reserved_base_page=%zu", bitmap_phys, bitmap, bitmap_size, bitmap_pages, reserved_base_page);

    // Mark the first page (0x0) as used to avoid null pointer confusion
    bitmap_set(0);

    boot_message(INFO, "PMM Initialized. Highest Address: 0x%lx, Bitmap Size: %lu bytes", highest_addr, bitmap_size);
}

uint64_t pmm_get_highest_addr(void)
{
    return highest_addr;
}

size_t pmm_get_reserved_base_page(void)
{
    return reserved_base_page;
}

uint64_t pmm_get_bitmap_phys(void)
{
    return (uint64_t)bitmap - pmm_hhdm_offset;
}

size_t pmm_get_bitmap_size(void)
{
    return bitmap_size;
}

void *pmm_alloc_page(void)
{
    void *addr = nullptr;
    uint64_t rflags;
    SPIN_LOCK_IRQSAVE(pmm_lock, rflags);

    for (size_t i = reserved_base_page; i < highest_page; i++)
    {
        if (!bitmap_test(i))
        {
            uintptr_t phys = i * PAGE_SIZE;

            // Never hand out a page that backs a slab header/payload.
            if (heap_is_slab_page((void *)phys))
            {
                boot_message(ERROR, "pmm_alloc_page: bitmap free but slab-tracked phys=%p", (void *)phys);
                continue;
            }

            bitmap_set(i);
            addr = (void *)phys;
            break;
        }
    }

    SPIN_UNLOCK_IRQRESTORE(pmm_lock, rflags);
    return addr; // nullptr when out of memory
}

void pmm_free_page(void *ptr)
{
    if (!ptr)
        return;

    uint64_t rflags;
    SPIN_LOCK_IRQSAVE(pmm_lock, rflags);

    uint64_t addr = (uint64_t)ptr;
    uintptr_t phys_ptr = (addr >= pmm_hhdm_offset) ? (addr - pmm_hhdm_offset) : addr;
    size_t page = phys_ptr / PAGE_SIZE;

    if (heap_is_slab_page((void *)phys_ptr))
    {
        boot_message(ERROR, "pmm_free_page: attempt to free slab page phys=%p", (void *)phys_ptr);
        SPIN_UNLOCK_IRQRESTORE(pmm_lock, rflags);
        return; // Ignore to avoid reusing active slab backing page
    }

    bitmap_unset(page);
    SPIN_UNLOCK_IRQRESTORE(pmm_lock, rflags);
}

void *pmm_alloc_pages(size_t count)
{
    void *addr = nullptr;
    uint64_t rflags;
    SPIN_LOCK_IRQSAVE(pmm_lock, rflags);

    // Simple first-fit search for contiguous pages
    for (size_t i = reserved_base_page; i < highest_page; i++)
    {
        if (!bitmap_test(i))
        {
            size_t free_count = 0;
            for (size_t j = 0; j < count; j++)
            {
                if (i + j < highest_page && !bitmap_test(i + j))
                {
                    uintptr_t phys = (i + j) * PAGE_SIZE;

                    // Skip ranges that overlap slab backing pages.
                    if (heap_is_slab_page((void *)phys))
                    {
                        boot_message(ERROR, "pmm_alloc_pages: bitmap free but slab-tracked phys=%p count=%zu", (void *)phys, count);
                        free_count = 0;
                        break;
                    }

                    free_count++;
                }
                else
                {
                    break;
                }
            }

            if (free_count == count)
            {
                for (size_t j = 0; j < count; j++)
                {
                    bitmap_set(i + j);
                }
                addr = (void *)(i * PAGE_SIZE);
                break;
            }
            else
            {
                i += free_count; // Skip checked pages
            }
        }
    }

    SPIN_UNLOCK_IRQRESTORE(pmm_lock, rflags);
    return addr;
}

void pmm_free_pages(void *ptr, size_t count)
{
    if (!ptr || count == 0)
        return;

    uint64_t rflags;
    SPIN_LOCK_IRQSAVE(pmm_lock, rflags);

    uint64_t addr = (uint64_t)ptr;
    uintptr_t phys_ptr = (addr >= pmm_hhdm_offset) ? (addr - pmm_hhdm_offset) : addr;
    size_t page = phys_ptr / PAGE_SIZE;

    if (heap_is_slab_page((void *)phys_ptr))
    {
        boot_message(ERROR, "pmm_free_pages: attempt to free slab page phys=%p count=%zu", (void *)phys_ptr, count);
        SPIN_UNLOCK_IRQRESTORE(pmm_lock, rflags);
        return; // Prevent reuse; likely a double free or corruption
    }

    for (size_t i = 0; i < count; i++)
    {
        bitmap_unset(page + i);
    }
    SPIN_UNLOCK_IRQRESTORE(pmm_lock, rflags);
}
