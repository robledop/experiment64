#include <drivers/framebuffer.h>
#include <lib/util.h>
#include <mem/vmm.h>
#include <sys/mman.h>
#include <syscall_common.h>
#include <task/process.h>

void* sys_mmap(void* addr, size_t length, int prot, int flags, int fd, size_t offset)
{
    if (length == 0)
        return MAP_FAILED;

    const bool is_anon = (flags & MAP_ANONYMOUS) != 0;
    const bool is_shared = (flags & MAP_SHARED) != 0;
    const bool is_private = (flags & MAP_PRIVATE) != 0;

    uint32_t vma_flags = VMA_USER | VMA_MMAP;
    if (prot & PROT_READ)
        vma_flags |= VMA_READ;
    if (prot & PROT_WRITE)
        vma_flags |= VMA_WRITE;
    if (prot & PROT_EXEC)
        vma_flags |= VMA_EXEC;

    uint64_t total_len = 0;
    uint64_t in_page_delta = 0;
    uint64_t phys_base = 0;

    if (is_anon)
    {
        if (!is_shared && !is_private)
            return MAP_FAILED;
        if (offset != 0)
            return MAP_FAILED;
        total_len = align_up(length, PAGE_SIZE);
        if (total_len < length)
            return MAP_FAILED;
        vma_flags |= VMA_ANON;
    }
    else
    {
        // Only support shared mappings of /dev/fb0 for now.
        if (!is_shared)
            return MAP_FAILED;

        if (fd < 0 || fd >= MAX_FDS)
            return MAP_FAILED;

        file_descriptor_t* desc = fd_get(fd);
        if (!desc)
            return MAP_FAILED;
        if (!desc->inode)
        {
            fd_put(desc);
            return MAP_FAILED;
        }

        // Require this to be the framebuffer device.
        struct limine_framebuffer* fb = framebuffer_current();
        if (!fb || desc->inode->device != fb)
        {
            fd_put(desc);
            return MAP_FAILED;
        }

        uint64_t fb_size = (uint64_t)fb->pitch * fb->height;
        if (offset >= fb_size)
        {
            fd_put(desc);
            return MAP_FAILED;
        }

        uint64_t map_len = length;
        if (offset + map_len > fb_size)
            map_len = fb_size - offset;

        uint64_t page_len = align_up(map_len, PAGE_SIZE);
        uint64_t page_offset = offset & ~(PAGE_SIZE - 1);
        in_page_delta = offset - page_offset;
        total_len = page_len + in_page_delta;
        if (total_len < map_len)
        {
            fd_put(desc);
            return MAP_FAILED;
        }

        uint64_t fb_addr = (uint64_t)fb->address;
        phys_base = (fb_addr >= g_hhdm_offset) ? (fb_addr - g_hhdm_offset) : fb_addr;
        phys_base += page_offset;
        fd_put(desc);
    }

    // Choose a base address if none provided.
    uint64_t base = (uint64_t)addr;
    if (base == 0)
        base = 0x4000000000; // simple search base for mmaps

    base = align_up(base, PAGE_SIZE);
    if (base + total_len < base)
        return MAP_FAILED;

    // Ensure no overlap with existing VMAs.
    while (true)
    {
        bool overlap = false;
        spinlock_acquire(&current_process->vm_lock);
        vm_area_t* area;
        list_foreach_entry(area, &current_process->vm_areas, list)
        {
            if (!(base + total_len <= area->start || base >= area->end))
            {
                overlap = true;
                base = align_up(area->end, PAGE_SIZE);
                break;
            }
        }
        spinlock_release(&current_process->vm_lock);
        if (!overlap)
            break;
        if (base >= 0x7FFFFFFFF000)
            return MAP_FAILED;
    }

    if (base + total_len > 0x7FFFFFFFF000)
        return MAP_FAILED;

    if (is_anon)
    {
        if (prot == PROT_NONE)
        {
            if (!vm_area_add(current_process, base, base + total_len, vma_flags))
                return MAP_FAILED;
            return (void*)(base + in_page_delta);
        }

        if (!map_user_anonymous_range(current_process, current_process->pml4, base, total_len, vma_flags))
            return MAP_FAILED;
        return (void*)(base + in_page_delta);
    }

    uint64_t virt = base;
    uint64_t phys = phys_base;
    uint64_t bytes_mapped = 0;

    while (bytes_mapped < total_len)
    {
        vmm_map_page(current_process->pml4, virt, phys, PTE_PRESENT | PTE_USER | PTE_WRITABLE);
        virt += PAGE_SIZE;
        phys += PAGE_SIZE;
        bytes_mapped += PAGE_SIZE;
    }

    if (!vm_area_add(current_process, base, base + total_len, vma_flags))
    {
        for (uint64_t unmap_va = base; unmap_va < base + total_len; unmap_va += PAGE_SIZE)
            vmm_unmap_page(current_process->pml4, unmap_va);
        return MAP_FAILED;
    }

    return (void*)(base + in_page_delta);
}
