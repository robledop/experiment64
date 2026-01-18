#include <mem/pmm.h>
#include <mem/vmm.h>
#include <task/process.h>
#include <lib/string.h>

int64_t sys_sbrk(int64_t increment)
{
    uint64_t old_brk = current_process->heap_end;
    uint64_t new_brk = old_brk + increment;

    // Align to page size for mapping
    uint64_t old_page_end = (old_brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint64_t new_page_end = (new_brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    if (increment > 0)
    {
        for (uint64_t addr = old_page_end; addr < new_page_end; addr += PAGE_SIZE)
        {
            void* phys = pmm_alloc_page();
            if (!phys)
            {
                return -1; // OOM
            }
            vmm_map_page(current_process->pml4, addr, (uint64_t)phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
            memset((void*)addr, 0, PAGE_SIZE);
        }
    }
    else if (increment < 0)
    {
        // Shrinking heap
        // We could unmap pages here if we wanted to be thorough
    }

    current_process->heap_end = new_brk;
    return (int64_t)old_brk;
}
