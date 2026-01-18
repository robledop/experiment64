#include <drivers/framebuffer.h>
#include <lib/util.h>
#include <mem/vmm.h>
#include <sys/mman.h>
#include <task/process.h>

void* sys_mmap(void* addr, size_t length, int prot, int flags, int fd, size_t offset)
{
    (void)prot;
    if (length == 0)
        return MAP_FAILED;

    // Only support shared mappings of /dev/fb0 for now.
    if (!(flags & MAP_SHARED))
        return MAP_FAILED;

    if (fd < 0 || fd >= MAX_FDS)
        return MAP_FAILED;

    file_descriptor_t* desc = current_process->fd_table[fd];
    if (!desc || !desc->inode)
        return MAP_FAILED;

    // Require this to be the framebuffer device.
    struct limine_framebuffer* fb = framebuffer_current();
    if (!fb || desc->inode->device != fb)
        return MAP_FAILED;

    uint64_t fb_size = (uint64_t)fb->pitch * fb->height;
    if (offset >= fb_size)
        return MAP_FAILED;

    uint64_t map_len = length;
    if (offset + map_len > fb_size)
        map_len = fb_size - offset;

    uint64_t page_len = align_up(map_len, PAGE_SIZE);
    uint64_t page_offset = offset & ~(PAGE_SIZE - 1);
    uint64_t in_page_delta = offset - page_offset;
    uint64_t total_len = page_len + in_page_delta;

    // Choose a base address if none provided.
    uint64_t base = (uint64_t)addr;
    if (base == 0)
        base = 0x4000000000; // simple search base for mmaps

    base = align_up(base, PAGE_SIZE);

    // Ensure no overlap with existing VMAs.
    while (true)
    {
        bool overlap = false;
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
        if (!overlap)
            break;
        if (base >= 0x7FFFFFFFF000)
            return MAP_FAILED;
    }

    uint64_t fb_addr = (uint64_t)fb->address;
    uint64_t phys_base = (fb_addr >= g_hhdm_offset) ? (fb_addr - g_hhdm_offset) : fb_addr;

    uint64_t virt = base;
    uint64_t phys = phys_base + page_offset;
    uint64_t bytes_mapped = 0;

    while (bytes_mapped < total_len)
    {
        vmm_map_page(current_process->pml4, virt, phys, PTE_PRESENT | PTE_USER | PTE_WRITABLE);
        virt += PAGE_SIZE;
        phys += PAGE_SIZE;
        bytes_mapped += PAGE_SIZE;
    }

    vm_area_add(current_process, base, base + total_len, VMA_READ | VMA_WRITE | VMA_USER | VMA_MMAP);

    return (void*)(base + in_page_delta);
}
