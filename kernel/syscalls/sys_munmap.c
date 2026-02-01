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
    uint64_t len_aligned = align_up(length, PAGE_SIZE);
    if (len_aligned < length)
        return -1;
    uint64_t end = start + len_aligned;
    if (end < start)
        return -1;

    constexpr uint32_t allowed_flags = VMA_MMAP | VMA_STACK;
    bool unmapped = false;
    int result = -1;

    spinlock_acquire(&current_process->vm_lock);
    vm_area_t* area;
    vm_area_t* tmp;
    list_foreach_entry_safe(area, tmp, &current_process->vm_areas, list)
    {
        if (!area || !(area->flags & allowed_flags))
            continue;

        const uint64_t overlap_start = max(start, area->start);
        const uint64_t overlap_end = min(end, area->end);
        if (overlap_start >= overlap_end)
            continue;

        vm_area_t* tail = nullptr;
        if (overlap_start > area->start && overlap_end < area->end)
        {
            tail = kmalloc(sizeof(vm_area_t));
            if (!tail)
                goto out;
            tail->start = overlap_end;
            tail->end = area->end;
            tail->flags = area->flags;
        }

        const bool is_anon = (area->flags & VMA_ANON) != 0;
        if (is_anon)
        {
            for (uint64_t va = overlap_start; va < overlap_end; va += PAGE_SIZE)
            {
                uint64_t phys = vmm_virt_to_phys(current_process->pml4, va);
                if (phys)
                    pmm_free_page((void*)(phys & ~(PAGE_SIZE - 1)));
            }
        }

        for (uint64_t va = overlap_start; va < overlap_end; va += PAGE_SIZE)
            vmm_unmap_page(current_process->pml4, va);

        unmapped = true;

        if (overlap_start == area->start && overlap_end == area->end)
        {
            list_del(&area->list);
            kfree(area);
            current_process->vm_area_count--;
            continue;
        }

        if (overlap_start == area->start)
        {
            area->start = overlap_end;
            continue;
        }

        if (overlap_end == area->end)
        {
            area->end = overlap_start;
            continue;
        }

        area->end = overlap_start;
        list_add(&tail->list, &area->list);
        current_process->vm_area_count++;
    }

    result = unmapped ? 0 : -1;

out:
    spinlock_release(&current_process->vm_lock);
    return result;
}
