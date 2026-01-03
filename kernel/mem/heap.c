#include "heap.h"
#include "pmm.h"
#include "string.h"
#include "terminal.h"
#include "list.h"
#include "spinlock.h"

#define HEAP_MAGIC 0xC0FFEE1234567890
#define SLAB_MIN_SIZE 32
#define SLAB_MAX_SIZE 2048
#define POISON_FREE 0xAA
#define POISON_ALLOC 0xCC
#define SLAB_GUARD 0x5A
#define SLAB_GUARD_LEN 64

static inline void slab_fill(void *dst, int c, size_t n)
{
    uint8_t *p = dst;
    while (n--)
    {
        *p++ = (uint8_t)c;
    }
}

static inline size_t align_up_size(size_t val, size_t align)
{
    return (val + align - 1) & ~(align - 1);
}

extern uint64_t g_hhdm_offset;
static spinlock_t heap_lock;
// Track slab backing pages to detect accidental pmm frees
#define SLAB_TRACK_MAX 4096
static void *slab_pages[SLAB_TRACK_MAX];
static size_t slab_pages_count = 0;

typedef struct slab_header
{
    uint64_t magic;
    uint64_t guard_magic;
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
static bool slab_guard_valid(slab_header_t *slab);
#ifdef HEAP_TRACE
static void slab_hexdump(slab_header_t *slab, size_t bytes);
#endif
static inline bool range_overlaps(uintptr_t start, uintptr_t end, uintptr_t region_start, uintptr_t region_end)
{
    return !(end <= region_start || start >= region_end);
}

static size_t slab_data_offset(void)
{
    // Align slab payload area to 64 bytes so objects (e.g., FPU state) meet XSAVE/FXSAVE requirements.
    // Insert a guard region after the header to detect underflow into metadata.
    return align_up_size(sizeof(slab_header_t) + SLAB_GUARD_LEN, 64);
}

static size_t slab_guard_size(void)
{
    return slab_data_offset() - sizeof(slab_header_t);
}

static void slab_track_add(void *slab_virt)
{
    if (slab_pages_count >= SLAB_TRACK_MAX)
        return;
    void *phys = (void *)((uint64_t)slab_virt - g_hhdm_offset);
    slab_pages[slab_pages_count++] = phys;
}

static void slab_track_remove(void *slab_virt)
{
    void *phys = (void *)((uint64_t)slab_virt - g_hhdm_offset);
    for (size_t i = 0; i < slab_pages_count; i++)
    {
        if (slab_pages[i] == phys)
        {
            slab_pages[i] = slab_pages[--slab_pages_count];
            return;
        }
    }
}

bool heap_is_slab_page(void *phys)
{
    for (size_t i = 0; i < slab_pages_count; i++)
    {
        if (slab_pages[i] == phys)
            return true;
    }
    return false;
}

bool heap_is_slab_range(const void *virt_ptr, size_t len)
{
    if (!virt_ptr || len == 0)
        return false;

    uintptr_t start = (uintptr_t)virt_ptr;
    uintptr_t end = start + len;

    for (size_t i = 0; i < slab_pages_count; i++)
    {
        uintptr_t slab_virt = (uintptr_t)slab_pages[i] + g_hhdm_offset;
        uintptr_t slab_end = slab_virt + PAGE_SIZE;
        if (!(end <= slab_virt || start >= slab_end))
        {
            return true;
        }
    }
    return false;
}

bool heap_is_slab_header_range(const void *virt_ptr, size_t len)
{
    if (!virt_ptr || len == 0)
        return false;

    uintptr_t start = (uintptr_t)virt_ptr;
    uintptr_t end = start + len;
    if (end < start)
    {
        end = UINTPTR_MAX; // Overflow wrap
    }
    size_t header_span = slab_data_offset();

    for (size_t i = 0; i < slab_pages_count; i++)
    {
        uintptr_t slab_phys = (uintptr_t)slab_pages[i];

        uintptr_t slab_virt = slab_phys + g_hhdm_offset;
        uintptr_t slab_head_end = slab_virt + header_span;
        if (range_overlaps(start, end, slab_virt, slab_head_end))
        {
            return true;
        }

        uintptr_t slab_identity = slab_phys;
        uintptr_t slab_identity_end = slab_identity + header_span;
        if (range_overlaps(start, end, slab_identity, slab_identity_end))
        {
            return true;
        }
    }
    return false;
}

static bool slab_guard_valid(slab_header_t *slab)
{
    uint8_t *guard = (uint8_t *)slab + sizeof(slab_header_t);
    size_t guard_len = slab_guard_size();
    for (size_t i = 0; i < guard_len; i++)
    {
        if (guard[i] != SLAB_GUARD)
        {
            return false;
        }
    }
    return true;
}

#ifdef HEAP_TRACE
static void slab_hexdump(slab_header_t *slab, size_t bytes)
{
    uint8_t *base = (uint8_t *)slab;
    size_t limit = (bytes > PAGE_SIZE) ? PAGE_SIZE : bytes;
    char line[3 * 16 + 1];
    for (size_t i = 0; i < limit; i += 16)
    {
        size_t chunk = (i + 16 <= limit) ? 16 : (limit - i);
        char *p = line;
        for (size_t j = 0; j < chunk; j++)
        {
            unsigned v = base[i + j];
            *p++ = "0123456789abcdef"[(v >> 4) & 0xF];
            *p++ = "0123456789abcdef"[v & 0xF];
            *p++ = ' ';
        }
        *p = '\0';
        boot_message(INFO, "heap: slab dump %p+0x%zx: %s", slab, i, line);
    }
}
#endif

#define SLOT_USER_PTR(slot) (slot)
#define SLOT_BASE_FROM_USER(ptr) ((uint8_t *)(ptr))
#define SLOT_USER_SIZE(slot_size) (slot_size)

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
    // Enforce supervisor write-protect so RO PTEs fault on kernel writes
    uint64_t cr0;
    __asm__ volatile("mov %0, cr0" : "=r"(cr0));
    cr0 |= (1ull << 16); // CR0.WP
    __asm__ volatile("mov cr0, %0" : : "r"(cr0) : "memory");

    spinlock_init(&heap_lock);
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
        return nullptr;

    void *virt = (void *)((uint64_t)phys + g_hhdm_offset);
    slab_header_t *header = (slab_header_t *)virt;

    header->magic = HEAP_MAGIC;
    header->guard_magic = HEAP_MAGIC;
    header->is_slab = 0;
    header->page_count = pages;
    header->obj_size = size;

    uint8_t *base = (uint8_t *)virt + sizeof(slab_header_t);
    void *user = base;
    return user;
}

static void *alloc_slab(int index)
{
    slab_header_t *slab = nullptr;
    slab_header_t *iter;

    // Find a slab with free objects
    list_for_each_entry(iter, &slab_caches[index], list)
    {
        if (iter->magic != HEAP_MAGIC || iter->guard_magic != HEAP_MAGIC || !iter->is_slab)
        {
            boot_message(ERROR, "heap: slab header corrupt cache=%d slab=%p magic=%lx guard=%lx is_slab=%u free_list=%p obj=%zu free_count=%zu", index, iter, iter->magic, iter->guard_magic, iter->is_slab, iter->free_list, iter->obj_size, iter->free_count);
            return nullptr;
        }

        if (!slab_guard_valid(iter))
        {
            boot_message(ERROR, "heap: slab guard corrupt cache=%d slab=%p", index, iter);
            return nullptr;
        }

        size_t expected_size = get_cache_size(index);
        if (iter->obj_size != expected_size)
        {
            boot_message(ERROR, "heap: slab obj_size mismatch cache=%d slab=%p obj=%zu expected=%zu", index, iter, iter->obj_size, expected_size);
            continue;
        }

        if (!iter->free_list && iter->free_count > 0)
        {
            boot_message(ERROR, "heap: slab free_list null with free_count cache=%d slab=%p", index, iter);
            continue;
        }

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
            return nullptr;

        void *virt = (void *)((uint64_t)phys + g_hhdm_offset);
        slab = (slab_header_t *)virt;

        slab->magic = HEAP_MAGIC;
        slab->guard_magic = HEAP_MAGIC;
        slab->is_slab = 1;
        slab->obj_size = get_cache_size(index);
        INIT_LIST_HEAD(&slab->list);

        // Initialize guard region between metadata and payload
        slab_fill((uint8_t *)slab + sizeof(slab_header_t), SLAB_GUARD, slab_guard_size());

        // Track backing page to catch accidental pmm frees
        slab_track_add(slab);

        // Initialize free list
        size_t available_size = PAGE_SIZE - slab_data_offset();
        size_t max_objects = available_size / slab->obj_size;
        slab->free_count = max_objects;

        uint8_t *base = (uint8_t *)virt + slab_data_offset();
        slab->free_list = base;

        // Link free objects and poison payload (skip link pointer)
        size_t link_size = sizeof(void *);
        size_t payload_size = (slab->obj_size > link_size) ? (slab->obj_size - link_size) : 0;
        for (size_t i = 0; i < max_objects; i++)
        {
            uint8_t *slot = base + i * slab->obj_size;
            void **obj = (void **)slot;
            *obj = (i + 1 < max_objects) ? (slot + slab->obj_size) : nullptr;
            if (payload_size)
            {
                slab_fill(slot + link_size, POISON_FREE, payload_size);
            }
        }

#ifdef HEAP_TRACE
        boot_message(INFO, "heap: new slab cache=%d slab=%p obj=%zu count=%zu", index, slab, slab->obj_size, max_objects);
#endif

        list_add(&slab->list, &slab_caches[index]);
    }

    uint8_t *slot = slab->free_list;
    if (slab->magic != HEAP_MAGIC || !slab->is_slab)
    {
        boot_message(ERROR, "heap: slab header corrupt (magic/is_slab)");
        return nullptr;
    }

    uint8_t *page_start = (uint8_t *)slab;
    uint8_t *page_end = page_start + PAGE_SIZE;
    uint8_t *slot_min = page_start + slab_data_offset();
    if (slot < slot_min || slot >= page_end)
    {
        boot_message(ERROR, "heap: free_list pointer out of range (slot=%p start=%p end=%p)", slot, page_start, page_end);
        return nullptr;
    }

    slab->free_list = *(void **)slot;
    slab->free_count--;

    // Detect overwrite of a freed slot before reuse (payload only)
    size_t link_size = sizeof(void *);
    size_t payload_size = (slab->obj_size > link_size) ? (slab->obj_size - link_size) : 0;
    if (payload_size)
    {
        uint8_t *payload = slot + link_size;
        for (size_t i = 0; i < payload_size; i++)
        {
            if (payload[i] != POISON_FREE)
            {
                boot_message(ERROR, "heap: slot poison mismatch (cache=%d slab=%p slot=%p idx=%zu val=0x%x)", index, slab, slot, i + link_size, payload[i]);
                break;
            }
        }
        slab_fill(payload, POISON_ALLOC, payload_size);
    }

    return SLOT_USER_PTR(slot);
}

void *kmalloc(size_t size)
{
    if (size == 0)
        return nullptr;

    uint64_t flags;
    SPIN_LOCK_IRQSAVE(heap_lock, flags);

    void *result = nullptr;
    int index = get_cache_index(size);
    if (index >= 0)
    {
        result = alloc_slab(index);
    }
    else
    {
        result = alloc_big(size);
    }

    SPIN_UNLOCK_IRQRESTORE(heap_lock, flags);

    return result;
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

    uint64_t flags;
    SPIN_LOCK_IRQSAVE(heap_lock, flags);

    // Find page start
    uint64_t addr = (uint64_t)ptr;
    uint64_t page_start = addr & ~(PAGE_SIZE - 1);
    slab_header_t *header = (slab_header_t *)page_start;

    if (header->magic != HEAP_MAGIC || header->guard_magic != HEAP_MAGIC)
    {
        printk("kfree: Invalid pointer (magic/guard mismatch) %p magic=%lx guard=%lx\n", ptr, header->magic, header->guard_magic);
        SPIN_UNLOCK_IRQRESTORE(heap_lock, flags);
        return;
    }

    if (header->is_slab)
    {
        uint8_t *slot_base = SLOT_BASE_FROM_USER(ptr);
        uint8_t *page_start = (uint8_t *)header;
        uint8_t *page_end = page_start + PAGE_SIZE;
        uint8_t *slot_min = page_start + slab_data_offset();

        int index = get_cache_index(header->obj_size);
        if (index < 0)
        {
            boot_message(ERROR, "heap: kfree invalid slab obj_size=%zu ptr=%p", header->obj_size, ptr);
            SPIN_UNLOCK_IRQRESTORE(heap_lock, flags);
            return;
        }

        if (slot_base < slot_min || slot_base >= page_end)
        {
            boot_message(ERROR, "heap: kfree slot out of range (slot=%p start=%p end=%p)", slot_base, page_start, page_end);
            SPIN_UNLOCK_IRQRESTORE(heap_lock, flags);
            return;
        }

        *(void **)slot_base = header->free_list;
        header->free_list = slot_base;
        header->free_count++;

        // Re-poison freed slot payload (skip link pointer)
        size_t link_size = sizeof(void *);
        size_t payload_size = (header->obj_size > link_size) ? (header->obj_size - link_size) : 0;
        if (payload_size)
        {
            slab_fill(slot_base + link_size, POISON_FREE, payload_size);
        }

        // If slab is completely free, release the page.
        size_t capacity = (PAGE_SIZE - slab_data_offset()) / header->obj_size;
        if (header->free_count == capacity)
        {
            if (index == 1)
            {
                // Keep cache-1 slabs resident to simplify corruption tracking.
                SPIN_UNLOCK_IRQRESTORE(heap_lock, flags);
                return;
            }

            list_del(&header->list);
            slab_track_remove(header);
            uintptr_t phys_addr = (uintptr_t)page_start - g_hhdm_offset;
            void *phys = (void *)phys_addr;
            pmm_free_pages(phys, 1);
            SPIN_UNLOCK_IRQRESTORE(heap_lock, flags);
            return;
        }
    }
    else
    {
        // Big allocation
        uintptr_t phys_addr = (uintptr_t)page_start - g_hhdm_offset;
        void *phys = (void *)phys_addr;
        pmm_free_pages(phys, header->page_count);
    }

    SPIN_UNLOCK_IRQRESTORE(heap_lock, flags);
}

void *krealloc(void *ptr, size_t new_size)
{
    if (!ptr)
        return kmalloc(new_size);
    if (new_size == 0)
    {
        kfree(ptr);
        return nullptr;
    }

    uint64_t addr = (uint64_t)ptr;
    uint64_t page_start = addr & ~(PAGE_SIZE - 1);
    slab_header_t *header = (slab_header_t *)page_start;

    if (header->magic != HEAP_MAGIC)
    {
        boot_message(ERROR, "krealloc: Invalid pointer");
        return nullptr;
    }

    size_t old_size = header->obj_size;

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
