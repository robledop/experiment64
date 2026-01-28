#include <mem/dma.h>
#include <lib/string.h>
#include <mem/pmm.h>
#include <mem/vmm.h>

static inline bool dma_is_pow2(const size_t value)
{
    return value != 0 && (value & (value - 1u)) == 0;
}

bool dma_alloc_pages(const size_t bytes,
                     size_t alignment,
                     const size_t boundary,
                     uintptr_t *phys_out,
                     void **virt_out)
{
    if (!phys_out || !virt_out || bytes == 0) {
        return false;
    }

    if (alignment == 0 || alignment < PAGE_SIZE) {
        alignment = PAGE_SIZE;
    }
    if (!dma_is_pow2(alignment)) {
        return false;
    }

    if (boundary != 0) {
        if (boundary < PAGE_SIZE) {
            return false;
        }
        if (!dma_is_pow2(boundary)) {
            return false;
        }
        if (bytes > boundary) {
            return false;
        }
    }

    const size_t pages = (bytes + PAGE_SIZE - 1u) / PAGE_SIZE;
    size_t extra_pages = 0;
    if (alignment > PAGE_SIZE) {
        extra_pages += (alignment / PAGE_SIZE) - 1u;
    }
    if (boundary > PAGE_SIZE) {
        extra_pages += (boundary / PAGE_SIZE) - 1u;
    }

    const size_t total_pages = pages + extra_pages;
    void *phys_block         = pmm_alloc_pages(total_pages);
    if (!phys_block) {
        return false;
    }

    const uintptr_t block     = (uintptr_t)phys_block;
    const uintptr_t block_end = block + (total_pages * PAGE_SIZE);
    uintptr_t selected        = 0;

    for (size_t page = 0; page + pages <= total_pages; page++) {
        const uintptr_t candidate = block + (page * PAGE_SIZE);
        if ((candidate & (alignment - 1u)) != 0) {
            continue;
        }

        if (boundary != 0) {
            const uintptr_t end = candidate + bytes - 1u;
            if ((candidate & ~(boundary - 1u)) != (end & ~(boundary - 1u))) {
                continue;
            }
        }

        selected = candidate;
        break;
    }

    if (selected == 0) {
        pmm_free_pages(phys_block, total_pages);
        return false;
    }

    const size_t prefix_pages = (selected - block) / PAGE_SIZE;
    if (prefix_pages != 0) {
        pmm_free_pages((void *)block, prefix_pages);
    }

    const uintptr_t kept_end     = selected + (pages * PAGE_SIZE);
    const size_t suffix_pages = (block_end - kept_end) / PAGE_SIZE;
    if (suffix_pages != 0) {
        pmm_free_pages((void *)kept_end, suffix_pages);
    }

    void *virt = (void *)(selected + g_hhdm_offset);
    memset(virt, 0, pages * PAGE_SIZE);

    *phys_out = selected;
    *virt_out = virt;
    return true;
}

void dma_free_pages(uintptr_t addr, size_t bytes)
{
    if (addr == 0 || bytes == 0) {
        return;
    }

    const size_t pages = (bytes + PAGE_SIZE - 1u) / PAGE_SIZE;
    pmm_free_pages((void *)addr, pages);
}
