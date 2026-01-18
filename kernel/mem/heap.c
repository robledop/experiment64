// ReSharper disable CppDFAConstantParameter
#include <mem/heap.h>
#include <mem/pmm.h>
#include <lib/string.h>
#include <drivers/terminal.h>
#include <lib/list.h>
#include <task/spinlock.h>

// https://en.wikipedia.org/wiki/Slab_allocation
// https://people.eecs.berkeley.edu/~kubitron/courses/cs194-24-S14/hand-outs/bonwick_slab.pdf
// https://hammertux.github.io/slab-allocator

#define HEAP_MAGIC 0xC0FFEE1234567890
#define SLAB_MIN_SIZE 32
#define SLAB_MAX_SIZE 2048
#define POISON_FREE 0xAA
#define POISON_ALLOC 0xCC
#define SLAB_GUARD 0x5A
#define SLAB_GUARD_LEN 64

static inline void slab_fill(void* dst, const int c, size_t n)
{
    uint8_t* p = dst;
    while (n--)
        *p++ = (uint8_t)c;
}

static inline size_t align_up_size(const size_t val, const size_t align)
{
    return (val + align - 1) & ~(align - 1);
}

extern uint64_t g_hhdm_offset;
static spinlock_t heap_lock;
// Track slab backing pages to detect accidental pmm frees
#define SLAB_TRACK_MAX 4096
static void* slab_pages[SLAB_TRACK_MAX];
static size_t slab_pages_count = 0;

typedef struct slab_header
{
    uint64_t magic;
    uint64_t guard_magic; // For redundancy
    uint8_t is_slab;
    uint8_t padding[7]; // Align
    // Slab specific
    list_item_t list;
    size_t obj_size;
    size_t free_count;
    void* free_list;
    // Big alloc specific
    size_t page_count;
} __attribute__((aligned(16))) slab_header_t;

// Cache for each size
// Sizes: 32, 64, 128, 256, 512, 1024, 2048
// Indices: 0, 1, 2, 3, 4, 5, 6
static const size_t slab_cache_sizes[] = {32, 64, 128, 256, 512, 1024, 2048};
#define CACHE_COUNT (sizeof(slab_cache_sizes) / sizeof(slab_cache_sizes[0]))

static list_item_t slab_caches[CACHE_COUNT];
static bool slab_guard_valid(slab_header_t* slab);

static inline bool range_overlaps(const uintptr_t start, const uintptr_t end, const uintptr_t region_start,
                                  const uintptr_t region_end)
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

static inline void* phys_to_virt(const void* phys)
{
    return (void*)((uintptr_t)phys + g_hhdm_offset);
}

static inline void* virt_to_phys(const void* virt)
{
    return (void*)((uintptr_t)virt - g_hhdm_offset);
}

typedef struct
{
    uint8_t* page_start;
    uint8_t* page_end;
    uint8_t* slot_min;
    size_t capacity;
    size_t payload_size;
} slab_layout_t;

static inline size_t slab_payload_size(const size_t obj_size)
{
    constexpr size_t link_size = sizeof(void*);
    return (obj_size > link_size) ? (obj_size - link_size) : 0;
}

static inline slab_layout_t slab_layout(slab_header_t* slab, const size_t obj_size)
{
    const size_t data_offset = slab_data_offset();
    slab_layout_t layout = {
        .page_start = (uint8_t*)slab,
        .page_end = (uint8_t*)slab + PAGE_SIZE,
        .slot_min = (uint8_t*)slab + data_offset,
        .capacity = (PAGE_SIZE - data_offset) / obj_size,
        .payload_size = slab_payload_size(obj_size),
    };
    return layout;
}

static void slab_track_add(const void* slab_virt)
{
    if (slab_pages_count >= SLAB_TRACK_MAX)
        return;
    void* phys = virt_to_phys(slab_virt);
    slab_pages[slab_pages_count++] = phys;
}

static void slab_track_remove(const void* slab_virt)
{
    const void* phys = virt_to_phys(slab_virt);
    for (size_t i = 0; i < slab_pages_count; i++)
    {
        if (slab_pages[i] == phys)
        {
            slab_pages[i] = slab_pages[--slab_pages_count];
            return;
        }
    }
}

bool heap_is_slab_page(const void* phys)
{
    for (size_t i = 0; i < slab_pages_count; i++)
    {
        if (slab_pages[i] == phys)
            return true;
    }
    return false;
}

bool heap_is_slab_range(const void* virt_ptr, const size_t len)
{
    if (!virt_ptr || len == 0)
        return false;

    const uintptr_t start = (uintptr_t)virt_ptr;
    const uintptr_t end = start + len;

    for (size_t i = 0; i < slab_pages_count; i++)
    {
        const uintptr_t slab_virt = (uintptr_t)phys_to_virt(slab_pages[i]);
        const uintptr_t slab_end = slab_virt + PAGE_SIZE;
        if (!(end <= slab_virt || start >= slab_end))
        {
            return true;
        }
    }
    return false;
}

bool heap_is_slab_header_range(const void* virt_ptr, const size_t len)
{
    if (!virt_ptr || len == 0)
        return false;

    const uintptr_t start = (uintptr_t)virt_ptr;
    uintptr_t end = start + len;
    if (end < start)
    {
        end = UINTPTR_MAX; // Overflow wrap
    }
    const size_t header_span = slab_data_offset();

    for (size_t i = 0; i < slab_pages_count; i++)
    {
        const uintptr_t slab_phys = (uintptr_t)slab_pages[i];
        const uintptr_t slab_virt = (uintptr_t)phys_to_virt(slab_pages[i]);
        const uintptr_t slab_head_end = slab_virt + header_span;
        if (range_overlaps(start, end, slab_virt, slab_head_end))
        {
            return true;
        }

        const uintptr_t slab_identity = slab_phys;
        const uintptr_t slab_identity_end = slab_identity + header_span;
        if (range_overlaps(start, end, slab_identity, slab_identity_end))
        {
            return true;
        }
    }
    return false;
}

static bool slab_guard_valid(slab_header_t* slab)
{
    const uint8_t* guard = (uint8_t*)slab + sizeof(slab_header_t);
    const size_t guard_len = slab_guard_size();
    for (size_t i = 0; i < guard_len; i++)
    {
        if (guard[i] != SLAB_GUARD) return false;
    }
    return true;
}

static bool slab_validate(const slab_header_t* slab, const int index)
{
    if (slab->magic != HEAP_MAGIC || slab->guard_magic != HEAP_MAGIC || !slab->is_slab)
    {
        boot_message(
            ERROR,
            "heap: slab header corrupt cache=%d slab=%p magic=%lx guard=%lx is_slab=%u free_list=%p obj=%zu free_count=%zu",
            index, slab, slab->magic, slab->guard_magic, slab->is_slab, slab->free_list, slab->obj_size,
            slab->free_count);
        return false;
    }

    if (!slab_guard_valid((slab_header_t*)slab))
    {
        boot_message(ERROR, "heap: slab guard corrupt cache=%d slab=%p", index, slab);
        return false;
    }

    return true;
}

static inline void slab_poison_payload(uint8_t* slot, const size_t payload_size, const uint8_t pattern)
{
    if (payload_size == 0) return;

    slab_fill(slot + sizeof(void*), pattern, payload_size);
}

static void slab_init_free_list(const slab_header_t* slab, uint8_t* base, const size_t capacity,
                                const size_t payload_size)
{
    const size_t obj_size = slab->obj_size;
    for (size_t i = 0; i < capacity; i++)
    {
        uint8_t* slot = base + i * obj_size;
        auto obj = (void**)slot;
        *obj = (i + 1 < capacity) ? (slot + obj_size) : nullptr;
        slab_poison_payload(slot, payload_size, POISON_FREE);
    }
}

static int get_cache_index(const size_t size)
{
    for (size_t i = 0; i < CACHE_COUNT; i++)
    {
        if (size <= slab_cache_sizes[i]) return (int)i;
    }
    return -1;
}

static size_t get_cache_size(const int index)
{
    return slab_cache_sizes[index];
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
    for (size_t i = 0; i < CACHE_COUNT; i++)
    {
        list_init_head(&slab_caches[i]);
    }
    boot_message(INFO, "Heap Initialized. HHDM Offset: 0x%lx", g_hhdm_offset);
}

static void* alloc_big(const size_t size)
{
    const size_t total_size = size + sizeof(slab_header_t);
    const size_t pages = (total_size + PAGE_SIZE - 1) / PAGE_SIZE;

    void* phys = pmm_alloc_pages(pages);
    if (!phys) return nullptr;

    void* virt = phys_to_virt(phys);
    auto header = (slab_header_t*)virt;

    header->magic = HEAP_MAGIC;
    header->guard_magic = HEAP_MAGIC;
    header->is_slab = 0;
    header->page_count = pages;
    header->obj_size = size;

    uint8_t* base = (uint8_t*)virt + sizeof(slab_header_t);
    void* user = base;
    return user;
}

static void* alloc_slab(const int index)
{
    slab_header_t* slab = nullptr;
    slab_header_t* iter;
    bool new_slab = false;

    // Find a slab with free objects
    list_foreach_entry(iter, &slab_caches[index], list)
    {
        if (!slab_validate(iter, index)) return nullptr;

        const size_t expected_size = get_cache_size(index);
        if (iter->obj_size != expected_size) // This can only happen if there's memory corruption
        {
            boot_message(ERROR, "heap: slab obj_size mismatch cache=%d slab=%p obj=%zu expected=%zu", index, iter,
                         iter->obj_size, expected_size);
            continue;
        }

        if (!iter->free_list && iter->free_count > 0) // This also indicates memory corruption
        {
            boot_message(ERROR, "heap: slab free_list null with free_count cache=%d slab=%p", index, iter);
            continue;
        }

        // Actually find a free slab
        if (iter->free_count > 0)
        {
            slab = iter;
            break;
        }
    }

    if (!slab) // Slab not found, allocate a new one
    {
        void* phys = pmm_alloc_page();
        if (!phys) return nullptr;

        void* virt = phys_to_virt(phys);
        slab = (slab_header_t*)virt;
        new_slab = true;

        slab->magic = HEAP_MAGIC;
        slab->guard_magic = HEAP_MAGIC;
        slab->is_slab = 1;
        slab->obj_size = get_cache_size(index);
        list_init_head(&slab->list);

        // Initialize a guard region between metadata and payload
        slab_fill((uint8_t*)slab + sizeof(slab_header_t), SLAB_GUARD, slab_guard_size());

        // Track the backing page to catch accidental pmm frees
        slab_track_add(slab);

        list_add(&slab->list, &slab_caches[index]);
    }

    slab_layout_t layout = slab_layout(slab, slab->obj_size);
    if (new_slab)
    {
        slab->free_count = layout.capacity;
        slab->free_list = layout.slot_min;
        slab_init_free_list(slab, layout.slot_min, layout.capacity, layout.payload_size);
    }

    uint8_t* slot = slab->free_list;
    if (slot < layout.slot_min || slot >= layout.page_end)
    {
        boot_message(ERROR, "heap: free_list pointer out of range (slot=%p start=%p end=%p)", slot, layout.page_start,
                     layout.page_end);
        return nullptr;
    }

    slab->free_list = *(void**)slot;
    slab->free_count--;

    // Detect overwrite of a freed slot before reuse (payload only)
    if (layout.payload_size)
    {
        constexpr size_t link_size = sizeof(void*);
        uint8_t* payload = slot + link_size;
        for (size_t i = 0; i < layout.payload_size; i++)
        {
            if (payload[i] != POISON_FREE)
            {
                boot_message(ERROR, "heap: slot poison mismatch (cache=%d slab=%p slot=%p idx=%zu val=0x%x)", index,
                             slab, slot, i + link_size, payload[i]);
                break;
            }
        }
        slab_poison_payload(slot, layout.payload_size, POISON_ALLOC);
    }

    return slot;
}

void* kmalloc(size_t size)
{
    if (size == 0)
        return nullptr;

    uint64_t flags;
    SPIN_LOCK_INT_SAVE(heap_lock, flags);

    void* result;
    int index = get_cache_index(size);
    if (index >= 0)
    {
        result = alloc_slab(index);
    }
    else
    {
        result = alloc_big(size);
    }

    SPIN_UNLOCK_INT_RESTORE(heap_lock, flags);

    return result;
}

void* kzalloc(const size_t size)
{
    void* ptr = kmalloc(size);
    if (ptr) memset(ptr, 0, size);
    return ptr;
}

void kfree(void* ptr)
{
    if (!ptr) return;

    uint64_t flags;
    SPIN_LOCK_INT_SAVE(heap_lock, flags);

    // Find page start
    uint64_t addr = (uint64_t)ptr;
    uint64_t page_start = addr & ~(PAGE_SIZE - 1);
    auto header = (slab_header_t*)page_start;

    if (header->magic != HEAP_MAGIC || header->guard_magic != HEAP_MAGIC)
    {
        printk("kfree: Invalid pointer (magic/guard mismatch) %p magic=%lx guard=%lx\n", ptr, header->magic,
               header->guard_magic);
        SPIN_UNLOCK_INT_RESTORE(heap_lock, flags);
        return;
    }

    if (header->is_slab)
    {
        auto slot_base = (uint8_t*)ptr;
        int index = get_cache_index(header->obj_size);
        if (index < 0)
        {
            boot_message(ERROR, "heap: kfree invalid slab obj_size=%zu ptr=%p", header->obj_size, ptr);
            SPIN_UNLOCK_INT_RESTORE(heap_lock, flags);
            return;
        }

        slab_layout_t layout = slab_layout(header, header->obj_size);
        if (slot_base < layout.slot_min || slot_base >= layout.page_end)
        {
            boot_message(ERROR, "heap: kfree slot out of range (slot=%p start=%p end=%p)", slot_base, layout.page_start,
                         layout.page_end);
            SPIN_UNLOCK_INT_RESTORE(heap_lock, flags);
            return;
        }

        *(void**)slot_base = header->free_list;
        header->free_list = slot_base;
        header->free_count++;

        // Re-poison the freed slot payload (skip the link pointer)
        slab_poison_payload(slot_base, layout.payload_size, POISON_FREE);

        // If the slab is completely free, release the page.
        if (header->free_count == layout.capacity)
        {
            if (index == 1)
            {
                // Keep cache-1 slabs resident to simplify corruption tracking.
                SPIN_UNLOCK_INT_RESTORE(heap_lock, flags);
                return;
            }

            list_del(&header->list);
            slab_track_remove(header);
            void* phys = virt_to_phys(layout.page_start);
            pmm_free_pages(phys, 1);
            SPIN_UNLOCK_INT_RESTORE(heap_lock, flags);
            return;
        }
    }
    else
    {
        // Big allocation
        void* phys = virt_to_phys((void*)page_start);
        pmm_free_pages(phys, header->page_count);
    }

    SPIN_UNLOCK_INT_RESTORE(heap_lock, flags);
}

void* krealloc(void* ptr, const size_t new_size)
{
    if (!ptr) return kmalloc(new_size);
    if (new_size == 0)
    {
        kfree(ptr);
        return nullptr;
    }

    const uint64_t addr = (uint64_t)ptr;
    const uint64_t page_start = addr & ~(PAGE_SIZE - 1);
    const slab_header_t* header = (slab_header_t*)page_start;

    if (header->magic != HEAP_MAGIC)
    {
        boot_message(ERROR, "krealloc: Invalid pointer");
        return nullptr;
    }

    const size_t old_size = header->obj_size;
    if (new_size <= old_size) return ptr; // Can reuse

    void* new_ptr = kmalloc(new_size);
    if (new_ptr)
    {
        memcpy(new_ptr, ptr, old_size);
        kfree(ptr);
    }
    return new_ptr;
}
