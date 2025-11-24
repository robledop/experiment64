#include "kasan.h"

#ifdef KASAN

#include "terminal.h"
#include "debug.h"
#include "string.h"
#include "vmm.h"
#include "pmm.h"

#define KASAN_POISON_ACCESSIBLE 0x00
#define KASAN_MAX_PHYS_COVER (1ULL << 30) // Cover up to 1 GiB for now

static uint64_t kasan_shadow_base = KASAN_SHADOW_OFFSET;
static uint64_t kasan_shadow_size = 0;
static uint64_t kasan_covered_start = 0;
static uint64_t kasan_covered_end = 0;
static bool kasan_ready = false;

static inline uint64_t align_up(uint64_t val, uint64_t align)
{
    return (val + align - 1) & ~(align - 1);
}

static inline uint64_t read_cr3_phys(void)
{
    uint64_t cr3;
    __asm__ volatile("mov %0, cr3" : "=r"(cr3));
    return cr3 & 0x000FFFFFFFFFF000ULL;
}

void kasan_early_init(uint64_t hhdm_offset, uint64_t phys_limit)
{
    uint64_t cover_bytes = phys_limit;
    if (cover_bytes > KASAN_MAX_PHYS_COVER)
        cover_bytes = KASAN_MAX_PHYS_COVER;

    kasan_covered_start = hhdm_offset;
    kasan_covered_end = hhdm_offset + cover_bytes;
    kasan_shadow_size = (cover_bytes + ((1ULL << KASAN_SHADOW_SCALE_SHIFT) - 1)) >> KASAN_SHADOW_SCALE_SHIFT;
    kasan_shadow_size = align_up(kasan_shadow_size, PAGE_SIZE);

    uint64_t cr3_phys = read_cr3_phys();
    uint64_t mapped = 0;

    for (uint64_t off = 0; off < kasan_shadow_size; off += PAGE_SIZE)
    {
        void *shadow_phys = pmm_alloc_page();
        if (!shadow_phys)
            break;

        void *shadow_virt = (void *)((uint64_t)shadow_phys + g_hhdm_offset);
        memset(shadow_virt, KASAN_POISON_FREE, PAGE_SIZE);

        vmm_map_page((pml4_t)cr3_phys, kasan_shadow_base + off, (uint64_t)shadow_phys, PTE_PRESENT | PTE_WRITABLE);
        mapped += PAGE_SIZE;
    }

    if (mapped < kasan_shadow_size)
    {
        boot_message(WARNING, "KASAN: shadow mapping partial (%lu/%lu)", mapped, kasan_shadow_size);
    }
    else
    {
        boot_message(INFO, "KASAN: shadow mapped base=0x%lx size=0x%lx covering 0x%lx bytes",
                     kasan_shadow_base, kasan_shadow_size, cover_bytes);
    }
    kasan_ready = true;
}

void kasan_poison_range(const void *addr, size_t size, uint8_t value)
{
    uintptr_t start = (uintptr_t)addr;
    uintptr_t end = start + size;

    if (start < kasan_covered_start || end > kasan_covered_end)
        return;

    uint8_t *shadow_start = (uint8_t *)(((start - kasan_covered_start) >> KASAN_SHADOW_SCALE_SHIFT) + kasan_shadow_base);
    uint8_t *shadow_end = (uint8_t *)(((end - kasan_covered_start + ((1 << KASAN_SHADOW_SCALE_SHIFT) - 1)) >> KASAN_SHADOW_SCALE_SHIFT) + kasan_shadow_base);
    size_t shadow_size = (size_t)(shadow_end - shadow_start);

    memset(shadow_start, value, shadow_size);
}

void kasan_unpoison_range(const void *addr, size_t size)
{
    kasan_poison_range(addr, size, KASAN_POISON_ACCESSIBLE);
}

bool kasan_check_range(const void *addr, size_t size, bool is_write, const void *ip)
{
    uintptr_t start = (uintptr_t)addr;
    uintptr_t end = start + size;

    if (start < kasan_covered_start || end > kasan_covered_end)
        return true; // Outside coverage; skip.

    for (size_t i = 0; i < size; i++)
    {
        uintptr_t cur = start + i;
        uint8_t *shadow = (uint8_t *)(((cur - kasan_covered_start) >> KASAN_SHADOW_SCALE_SHIFT) + kasan_shadow_base);
        if (*shadow != KASAN_POISON_ACCESSIBLE)
        {
            kasan_report((const void *)cur, 1, is_write, ip);
            return false;
        }
    }
    return true;
}

void kasan_report(const void *addr, size_t size, bool is_write, const void *ip)
{
    panic("KASAN: invalid %s of size %zu at %p (ip=%p)",
          is_write ? "write" : "read", size, addr, ip);
}

bool kasan_is_ready(void)
{
    return kasan_ready;
}

uint8_t kasan_shadow_value(const void *addr)
{
    if (!kasan_ready)
        return 0xFF;

    uintptr_t a = (uintptr_t)addr;
    if (a < kasan_covered_start || a >= kasan_covered_end)
        return 0xFF;

    uint8_t *shadow = (uint8_t *)(((a - kasan_covered_start) >> KASAN_SHADOW_SCALE_SHIFT) + kasan_shadow_base);
    return *shadow;
}

#endif
