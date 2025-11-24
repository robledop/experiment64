#include "heap.h"
#include "pmm.h"
#include "string.h"
#include "terminal.h"
#include "list.h"

#define HEAP_MAGIC 0xC0FFEE1234567890
#define SLAB_MIN_SIZE 32
#define SLAB_MAX_SIZE 2048

static uint64_t g_hhdm_offset = 0;

typedef struct slab_header
{
    uint64_t magic;
    uint8_t is_slab;
    uint8_t padding[7]; // Align
    // Slab specific
    list_head_t list;
    size_t obj_size;
    size_t free_count;
    void *free_list;
    // Big alloc specific
    size_t page_count;
} __attribute__((aligned(16))) slab_header_t;

// Cache for each size
// Sizes: 32, 64, 128, 256, 512, 1024, 2048
// Indices: 0, 1,  2,   3,   4,   5,    6
#define CACHE_COUNT 7

static list_head_t slab_caches[CACHE_COUNT];

static int get_cache_index(size_t size)
{
    if (size <= 32)
        return 0;
    if (size <= 64)
        return 1;
    if (size <= 128)
        return 2;
    if (size <= 256)
        return 3;
    if (size <= 512)
        return 4;
    if (size <= 1024)
        return 5;
    if (size <= 2048)
        return 6;
    return -1;
}

static size_t get_cache_size(int index)
{
    return 32 << index;
}

void heap_init(uint64_t hhdm_offset)
{
    g_hhdm_offset = hhdm_offset;
    for (int i = 0; i < CACHE_COUNT; i++)
    {
        INIT_LIST_HEAD(&slab_caches[i]);
    }
    boot_message(INFO, "Heap Initialized. HHDM Offset: 0x%lx", g_hhdm_offset);
}

static void *alloc_big(size_t size)
{
    size_t total_size = size + sizeof(slab_header_t);
    size_t pages = (total_size + PAGE_SIZE - 1) / PAGE_SIZE;

    void *phys = pmm_alloc_pages(pages);
    if (!phys)
        return NULL;

    void *virt = (void *)((uint64_t)phys + g_hhdm_offset);
    slab_header_t *header = (slab_header_t *)virt;

    header->magic = HEAP_MAGIC;
    header->is_slab = 0;
    header->page_count = pages;
    header->obj_size = size;

    return (void *)((uint64_t)virt + sizeof(slab_header_t));
}

static void *alloc_slab(int index)
{
    slab_header_t *slab = NULL;
    slab_header_t *iter;

    // Find a slab with free objects
    list_for_each_entry(iter, &slab_caches[index], list)
    {
        if (iter->free_count > 0)
        {
            slab = iter;
            break;
        }
    }

    if (!slab)
    {
        void *phys = pmm_alloc_page();
        if (!phys)
            return NULL;

        void *virt = (void *)((uint64_t)phys + g_hhdm_offset);
        slab = (slab_header_t *)virt;

        slab->magic = HEAP_MAGIC;
        slab->is_slab = 1;
        slab->obj_size = get_cache_size(index);
        INIT_LIST_HEAD(&slab->list);

        // Initialize free list
        size_t available_size = PAGE_SIZE - sizeof(slab_header_t);
        size_t max_objects = available_size / slab->obj_size;
        slab->free_count = max_objects;

        uint8_t *base = (uint8_t *)virt + sizeof(slab_header_t);
        slab->free_list = base;

        for (size_t i = 0; i < max_objects - 1; i++)
        {
            void **obj = (void **)(base + i * slab->obj_size);
            *obj = (base + (i + 1) * slab->obj_size);
        }

        void **last_obj = (void **)(base + (max_objects - 1) * slab->obj_size);
        *last_obj = NULL;

        list_add(&slab->list, &slab_caches[index]);
    }

    void *ptr = slab->free_list;
    slab->free_list = *(void **)ptr;
    slab->free_count--;

    return ptr;
}

void *kmalloc(size_t size)
{
    if (size == 0)
        return NULL;

    int index = get_cache_index(size);
    if (index >= 0)
    {
        return alloc_slab(index);
    }
    else
    {
        return alloc_big(size);
    }
}

void *kzalloc(size_t size)
{
    void *ptr = kmalloc(size);
    if (ptr)
    {
        memset(ptr, 0, size);
    }
    return ptr;
}

void kfree(void *ptr)
{
    if (!ptr)
        return;

    // Find page start
    uint64_t addr = (uint64_t)ptr;
    uint64_t page_start = addr & ~(PAGE_SIZE - 1);
    slab_header_t *header = (slab_header_t *)page_start;

    if (header->magic != HEAP_MAGIC)
    {
        printk("kfree: Invalid pointer (magic mismatch) %p\n", ptr);
        return;
    }

    if (header->is_slab)
    {
        *(void **)ptr = header->free_list;
        header->free_list = ptr;
        header->free_count++;

        // If slab is completely free, release the page.
        size_t capacity = (PAGE_SIZE - sizeof(slab_header_t)) / header->obj_size;
        if (header->free_count == capacity)
        {
            int index = get_cache_index(header->obj_size);
            if (index >= 0)
            {
                list_del(&header->list);
                // Free the page
                void *phys = (void *)(page_start - g_hhdm_offset);
                pmm_free_pages(phys, 1);
                return;
            }
        }
    }
    else
    {
        // Big allocation
        void *phys = (void *)(page_start - g_hhdm_offset);
        pmm_free_pages(phys, header->page_count);
    }
}

void *krealloc(void *ptr, size_t new_size)
{
    if (!ptr)
        return kmalloc(new_size);
    if (new_size == 0)
    {
        kfree(ptr);
        return NULL;
    }

    uint64_t addr = (uint64_t)ptr;
    uint64_t page_start = addr & ~(PAGE_SIZE - 1);
    slab_header_t *header = (slab_header_t *)page_start;

    if (header->magic != HEAP_MAGIC)
    {
        boot_message(ERROR, "krealloc: Invalid pointer");
        return NULL;
    }

    size_t old_size;
    if (header->is_slab)
    {
        old_size = header->obj_size;
    }
    else
    {
        old_size = header->obj_size; // We stored it there in alloc_big
    }

    if (new_size <= old_size)
    {
        return ptr; // Can reuse
    }

    void *new_ptr = kmalloc(new_size);
    if (new_ptr)
    {
        memcpy(new_ptr, ptr, old_size);
        kfree(ptr);
    }
    return new_ptr;
}
