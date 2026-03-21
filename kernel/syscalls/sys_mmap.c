#include <drivers/framebuffer.h>
#include <ipc/shm.h>
#include <lib/util.h>
#include <mem/pmm.h>
#include <mem/vmm.h>
#include <sys/mman.h>
#include <syscall_common.h>
#include <task/process.h>

/**
 * Find a suitable base address for a new memory mapping, starting from a hint and ensuring it does not overlap with
 * existing mappings.
 * @param hint The preferred base address for the mapping.
 * @param total_len The total length of the mapping.
 * @return The chosen base address, or 0 on failure.
 */
static uint64_t mmap_find_base(uint64_t hint, uint64_t total_len)
{
    uint64_t base = hint ? hint : 0x4000000000;
    base          = align_up(base, PAGE_SIZE);
    if (base + total_len < base)
        return 0;

    while (true) {
        bool overlap = false;
        spinlock_acquire(&current_process->vm_lock);
        vm_area_t *area;
        list_foreach_entry(area, &current_process->vm_areas, list)
        {
            if (!(base + total_len <= area->start || base >= area->end)) {
                overlap = true;
                base    = align_up(area->end, PAGE_SIZE);
                break;
            }
        }
        spinlock_release(&current_process->vm_lock);
        if (!overlap)
            break;
        if (base >= 0x7FFFFFFFF000)
            return 0;
    }

    if (base + total_len > 0x7FFFFFFFF000)
        return 0;

    return base;
}

/**
 * Map a shared memory object into the current process's address space.
 * @param shm The shared memory object to map.
 * @param base The preferred base address for the mapping.
 * @param length The length of the mapping.
 * @param offset The offset within the shared memory object.
 * @param vma_flags The flags for the virtual memory area.
 * @return A pointer to the mapped memory, or MAP_FAILED on error.
 */
static void *mmap_shm(shm_entry_t *shm, uint64_t base, size_t length, size_t offset, uint32_t vma_flags)
{
    if (offset >= shm->size)
        return MAP_FAILED;

    size_t map_len = length;
    if (offset + map_len > shm->size)
        map_len = shm->size - offset;

    uint64_t total_len = align_up(map_len, PAGE_SIZE);
    if (total_len < map_len)
        return MAP_FAILED;

    base = mmap_find_base(base, total_len);
    if (!base)
        return MAP_FAILED;

    size_t start_page = offset / PAGE_SIZE;
    size_t num_pages  = total_len / PAGE_SIZE;

    for (size_t i = 0; i < num_pages; i++) {
        if (start_page + i >= shm->num_pages)
            break;
        vmm_map_page(current_process->pml4,
                     base + i * PAGE_SIZE,
                     shm->phys_pages[start_page + i],
                     PTE_PRESENT | PTE_USER | PTE_WRITABLE | PTE_SHARED);
    }

    if (!vm_area_add(current_process, base, base + total_len, vma_flags)) {
        for (size_t i = 0; i < num_pages; i++)
            vmm_unmap_page(current_process->pml4, base + i * PAGE_SIZE);
        return MAP_FAILED;
    }

    uint64_t in_page_delta = offset - (offset & ~(PAGE_SIZE - 1));
    return (void *)(base + in_page_delta);
}

/**
 * Map a framebuffer device into the current process's address space if the file descriptor corresponds to the current
 * framebuffer.
 * @param base The preferred base address for the mapping.
 * @param length The length of the mapping.
 * @param offset The offset within the framebuffer.
 * @param desc The file descriptor for the framebuffer device.
 * @param vma_flags The flags for the virtual memory area.
 * @return A pointer to the mapped memory, or MAP_FAILED on error.
 */
static void *mmap_framebuffer(uint64_t base, size_t length, size_t offset, file_descriptor_t *desc, uint32_t vma_flags)
{
    struct limine_framebuffer *fb = framebuffer_current();
    if (!fb || desc->inode->device != fb)
        return MAP_FAILED;

    uint64_t fb_size = (uint64_t)fb->pitch * fb->height;
    if (offset >= fb_size)
        return MAP_FAILED;

    uint64_t map_len = length;
    if (offset + map_len > fb_size)
        map_len = fb_size - offset;

    uint64_t page_len      = align_up(map_len, PAGE_SIZE);
    uint64_t page_offset   = offset & ~(PAGE_SIZE - 1);
    uint64_t in_page_delta = offset - page_offset;
    uint64_t total_len     = page_len + in_page_delta;
    if (total_len < map_len)
        return MAP_FAILED;

    base = mmap_find_base(base, total_len);
    if (!base)
        return MAP_FAILED;

    uint64_t fb_addr   = (uint64_t)fb->address;
    uint64_t phys_base = (fb_addr >= g_hhdm_offset) ? (fb_addr - g_hhdm_offset) : fb_addr;
    phys_base += page_offset;

    uint64_t virt         = base;
    uint64_t phys         = phys_base;
    uint64_t bytes_mapped = 0;
    while (bytes_mapped < total_len) {
        vmm_map_page(current_process->pml4, virt, phys, PTE_PRESENT | PTE_USER | PTE_WRITABLE | PTE_SHARED);
        virt += PAGE_SIZE;
        phys += PAGE_SIZE;
        bytes_mapped += PAGE_SIZE;
    }

    if (!vm_area_add(current_process, base, base + total_len, vma_flags)) {
        for (uint64_t va = base; va < base + total_len; va += PAGE_SIZE)
            vmm_unmap_page(current_process->pml4, va);
        return MAP_FAILED;
    }

    return (void *)(base + in_page_delta);
}

/**
 * Handle the mmap system call to create a new memory mapping in the current process's address space. Supports anonymous
 * mappings, shared memory objects, and framebuffer mappings based on the provided flags and file descriptor.
 * @param addr The preferred base address for the mapping.
 * @param length The length of the mapping.
 * @param prot The desired memory protection for the mapping.
 * @param flags The flags for the mapping.
 * @param fd The file descriptor for the mapping.
 * @param offset The offset within the file or device for the mapping.
 * @return A pointer to the mapped memory, or MAP_FAILED on error.
 */
void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd, size_t offset)
{
    if (length == 0)
        return MAP_FAILED;

    const bool is_anon    = (flags & MAP_ANONYMOUS) != 0;
    const bool is_shared  = (flags & MAP_SHARED) != 0;
    const bool is_private = (flags & MAP_PRIVATE) != 0;

    uint32_t vma_flags = VMA_USER | VMA_MMAP;
    if (prot & PROT_READ)
        vma_flags |= VMA_READ;
    if (prot & PROT_WRITE)
        vma_flags |= VMA_WRITE;
    if (prot & PROT_EXEC)
        vma_flags |= VMA_EXEC;

    if (is_anon) {
        if (!is_shared && !is_private)
            return MAP_FAILED;
        if (offset != 0)
            return MAP_FAILED;
        uint64_t total_len = align_up(length, PAGE_SIZE);
        if (total_len < length)
            return MAP_FAILED;
        vma_flags |= VMA_ANON;

        uint64_t base = mmap_find_base((uint64_t)addr, total_len);
        if (!base)
            return MAP_FAILED;

        if (prot == PROT_NONE) {
            if (!vm_area_add(current_process, base, base + total_len, vma_flags))
                return MAP_FAILED;
            return (void *)base;
        }

        if (!map_user_anonymous_range(current_process, current_process->pml4, base, total_len, vma_flags))
            return MAP_FAILED;
        return (void *)base;
    }

    if (!is_shared)
        return MAP_FAILED;
    if (fd < 0 || fd >= MAX_FDS)
        return MAP_FAILED;

    file_descriptor_t *desc = fd_get(fd);
    if (!desc)
        return MAP_FAILED;
    if (!desc->inode) {
        fd_put(desc);
        return MAP_FAILED;
    }

    void *result;
    if (shm_is_shm_inode(desc->inode)) {
        auto shm = (shm_entry_t *)desc->inode->device;
        result   = mmap_shm(shm, (uint64_t)addr, length, offset, vma_flags);
    } else {
        result = mmap_framebuffer((uint64_t)addr, length, offset, desc, vma_flags);
    }

    fd_put(desc);
    return result;
}
