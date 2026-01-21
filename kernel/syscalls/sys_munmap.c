#include <debug.h>
#include <lib/util.h>
#include <mem/heap.h>
#include <mem/pmm.h>
#include <mem/vmm.h>
#include <task/process.h>

int sys_munmap(void* addr, size_t length)
{
    if (!addr || length == 0)
        return -1;

    uint64_t start = (uint64_t)addr & ~(PAGE_SIZE - 1);
    uint64_t end = start + align_up(length, PAGE_SIZE);

    vm_area_t* area;
    bool found = false;
    bool is_anon = false;
    list_foreach_entry(area, &current_process->vm_areas, list)
    {
        if (area->start == start && area->end == end && (area->flags & VMA_MMAP))
        {
            found = true;
            is_anon = (area->flags & VMA_ANON) != 0;
            break;
        }
    }
    if (!found)
        return -1;

    if (is_anon)
    {
        for (uint64_t va = start; va < end; va += PAGE_SIZE)
        {
            uint64_t phys = vmm_virt_to_phys(current_process->pml4, va);
            if (phys)
                pmm_free_page((void*)(phys & ~(PAGE_SIZE - 1)));
        }
    }

    for (uint64_t va = start; va < end; va += PAGE_SIZE)
        vmm_unmap_page(current_process->pml4, va);

    vm_area_t* tmp;
    list_foreach_entry_safe(area, tmp, &current_process->vm_areas, list)
    {
        if (!area)
            panic("%s: area is null", __func__);

        if (area->start == start && area->end == end && (area->flags & VMA_MMAP))
        {
            list_del(&area->list);
            kfree(area);
            current_process->vm_area_count--;
            break;
        }
    }
    return 0;
}
