#include <sys/syscall.h>
#include <syscall_common.h>
#include <arch/x86_64/cpu.h>
#include <mem/heap.h>
#include <mem/pmm.h>
#include <mem/vmm.h>

static vm_area_t* find_stack_vma(process_t* proc, uint64_t stack_ptr)
{
    if (!proc || stack_ptr == 0)
        return nullptr;

    vm_area_t* area;
    list_foreach_entry(area, &proc->vm_areas, list)
    {
        if ((area->flags & VMA_STACK) && stack_ptr >= area->start && stack_ptr < area->end)
            return area;
    }

    return nullptr;
}

static void free_stack_vma(process_t* proc, vm_area_t* area)
{
    if (!proc || !area)
        return;

    for (uint64_t addr = area->start; addr < area->end; addr += PAGE_SIZE)
    {
        uint64_t phys = vmm_virt_to_phys(proc->pml4, addr);
        vmm_unmap_page(proc->pml4, addr);
        if (phys)
            pmm_free_page((void*)phys);
    }

    list_del(&area->list);
    if (proc->vm_area_count > 0)
        proc->vm_area_count--;
    kfree(area);
}

void sys_thread_exit(int code)
{
    thread_t* self = current_thread;
    process_t* proc = current_process;
    if (!self || !self->is_user || !proc)
        return;

    TEST_SYSCALL_LOG("sys_thread_exit: pid=%d tid=%d code=%d\n",
                     proc->pid,
                     self->tid,
                     code);

    __asm__ volatile("cli");

    uint64_t stack_ptr = 0;
    cpu_t* cpu = get_cpu();
    if (cpu && cpu->user_rsp)
        stack_ptr = cpu->user_rsp;
    if (stack_ptr == 0 && self->saved_user_rsp)
        stack_ptr = self->saved_user_rsp;
    if (stack_ptr == 0)
        stack_ptr = self->user_stack;

    vm_area_t* stack_area = find_stack_vma(proc, stack_ptr);
    if (stack_area)
        free_stack_vma(proc, stack_area);

    spinlock_acquire(&scheduler_lock);

    self->state = THREAD_TERMINATED;

    bool last_thread = true;
    thread_t* t;
    list_foreach_entry(t, &proc->threads, list)
    {
        if (t != self && t->state != THREAD_TERMINATED)
        {
            last_thread = false;
            break;
        }
    }

    process_t* parent = nullptr;
    if (last_thread)
    {
        proc->exit_code = code;
        proc->terminated = true;

        process_t* new_parent = init_process ? init_process : kernel_process;
        process_t* child;
        list_foreach_entry(child, &process_list, list)
        {
            if (child && child->parent == proc)
            {
                child->parent = new_parent;
                if (child->terminated)
                    thread_wakeup(new_parent);
            }
        }

        parent = proc->parent;
    }

    spinlock_release(&scheduler_lock);

    if (parent)
        thread_wakeup(parent);

    schedule();
}
