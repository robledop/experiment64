#include "pmm.h"
#include "limine.h"
#include "string.h"
#include "terminal.h"

__attribute__((used, section(".requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};

static uint8_t *bitmap = NULL;
static size_t bitmap_size = 0;
static size_t highest_page = 0;

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

void pmm_init(void)
{
    if (memmap_request.response == NULL)
    {
        printf("Error: Limine memmap request failed\n");
        for (;;) __asm__("hlt");
    }

    struct limine_memmap_response *memmap = memmap_request.response;
    uint64_t highest_addr = 0;

    // 1. Find the highest usable physical address
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

    // 2. Find a place to put the bitmap
    for (uint64_t i = 0; i < memmap->entry_count; i++)
    {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE)
        {
            if (entry->length >= bitmap_size)
            {
                bitmap = (uint8_t *)entry->base;
                // Initialize bitmap to all 1s (used)
                memset(bitmap, 0xFF, bitmap_size);
                break;
            }
        }
    }

    if (bitmap == NULL)
    {
        printf("Error: Could not find memory for PMM bitmap\n");
        for (;;) __asm__("hlt");
    }

    // 3. Mark usable regions as free (0) in the bitmap
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

    // 4. Mark the bitmap itself as used
    uint64_t bitmap_start_page = (uint64_t)bitmap / PAGE_SIZE;
    uint64_t bitmap_pages = (bitmap_size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t i = 0; i < bitmap_pages; i++)
    {
        bitmap_set(bitmap_start_page + i);
    }

    // 5. Mark the first page (0x0) as used to avoid null pointer confusion
    bitmap_set(0);

    printf("PMM Initialized. Highest Address: 0x%lx, Bitmap Size: %lu bytes\n", highest_addr, bitmap_size);
}

void *pmm_alloc_page(void)
{
    for (size_t i = 0; i < highest_page; i++)
    {
        if (!bitmap_test(i))
        {
            bitmap_set(i);
            return (void *)(i * PAGE_SIZE);
        }
    }
    return NULL; // Out of memory
}

void pmm_free_page(void *ptr)
{
    uint64_t addr = (uint64_t)ptr;
    size_t page = addr / PAGE_SIZE;
    bitmap_unset(page);
}

void *pmm_alloc_pages(size_t count)
{
    // Simple first-fit search for contiguous pages
    for (size_t i = 0; i < highest_page; i++)
    {
        if (!bitmap_test(i))
        {
            size_t free_count = 0;
            for (size_t j = 0; j < count; j++)
            {
                if (i + j < highest_page && !bitmap_test(i + j))
                {
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
                return (void *)(i * PAGE_SIZE);
            }
            else
            {
                i += free_count; // Skip checked pages
            }
        }
    }
    return NULL;
}

void pmm_free_pages(void *ptr, size_t count)
{
    uint64_t addr = (uint64_t)ptr;
    size_t page = addr / PAGE_SIZE;
    for (size_t i = 0; i < count; i++)
    {
        bitmap_unset(page + i);
    }
}
