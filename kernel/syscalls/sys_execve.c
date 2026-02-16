#include <syscall_common.h>
#include <lib/elf.h>
#include <lib/string.h>
#include <mem/heap.h>
#include <mem/pmm.h>
#include <mem/vmm.h>
#include <sys/syscall.h>
#include <task/signal.h>

#define EXEC_MAX_ARGS 16
#define EXEC_MAX_ARG_LEN 128

static int copy_in_args(const char* const* argv, char args[EXEC_MAX_ARGS][EXEC_MAX_ARG_LEN])
{
    if (!argv)
        return 0;

    int count = 0;
    while (count < EXEC_MAX_ARGS)
    {
        const char* user_arg = nullptr;
        if (!copy_from_user(&user_arg, &argv[count], sizeof(user_arg)))
            return -1;
        if (!user_arg)
            break;
        if (!copy_from_user_str(args[count], user_arg, EXEC_MAX_ARG_LEN))
            return -1;
        count++;
    }
    return count;
}

// ReSharper disable once CppDFAConstantParameter
static void setup_user_stack(uint64_t stack_top,
                             const char args[EXEC_MAX_ARGS][EXEC_MAX_ARG_LEN], int argc, uint64_t* out_rsp)
{
    uint64_t sp = stack_top;
    uint64_t arg_ptrs[EXEC_MAX_ARGS];

    for (int i = argc - 1; i >= 0; i--)
    {
        size_t len = strlen(args[i]) + 1;
        sp -= len;
        memcpy((void*)sp, args[i], len);
        arg_ptrs[i] = sp;
    }

    // Align stack to 16 bytes
    sp &= ~0xFul;

    // argv terminator
    sp -= sizeof(uint64_t);
    *(uint64_t*)sp = 0;

    // argv pointers
    for (int i = argc - 1; i >= 0; i--)
    {
        sp -= sizeof(uint64_t);
        *(uint64_t*)sp = arg_ptrs[i];
    }

    // argc
    sp -= sizeof(uint64_t);
    *(uint64_t*)sp = (uint64_t)argc;

    *out_rsp = sp;
}

static void vm_area_detach(process_t* proc, list_item_t* out_head, uint32_t* out_count)
{
    if (!out_head || !out_count)
        return;

    list_init_head(out_head);
    *out_count = 0;
    if (!proc)
        return;

    spinlock_acquire(&proc->vm_lock);
    *out_count = proc->vm_area_count;
    if (list_empty(&proc->vm_areas))
    {
        proc->vm_area_count = 0;
        spinlock_release(&proc->vm_lock);
        return;
    }

    out_head->next = proc->vm_areas.next;
    out_head->prev = proc->vm_areas.prev;
    out_head->next->prev = out_head;
    out_head->prev->next = out_head;
    list_init_head(&proc->vm_areas);
    proc->vm_area_count = 0;
    spinlock_release(&proc->vm_lock);
}

static void vm_area_restore(process_t* proc, list_item_t* head, uint32_t count)
{
    if (!proc || !head)
        return;

    spinlock_acquire(&proc->vm_lock);
    if (list_empty(head))
    {
        list_init_head(&proc->vm_areas);
    }
    else
    {
        proc->vm_areas.next = head->next;
        proc->vm_areas.prev = head->prev;
        head->next->prev = &proc->vm_areas;
        head->prev->next = &proc->vm_areas;
    }
    proc->vm_area_count = count;
    spinlock_release(&proc->vm_lock);
    list_init_head(head);
}

static void vm_area_free_list(list_item_t* head)
{
    if (!head)
        return;

    vm_area_t* area;
    vm_area_t* tmp;
    list_foreach_entry_safe(area, tmp, head, list)
    {
        list_del(&area->list);
        kfree(area);
    }
    list_init_head(head);
}

int sys_execve(const char* path, const char* const argv[], [[maybe_unused]] const char* const envp[],
               struct syscall_regs* regs)
{
    if (!path)
        return -1;

    TEST_SYSCALL_LOG("sys_execve: enter pid=%d path_ptr=%p argv_ptr=%p envp_ptr=%p\n",
                     current_process ? current_process->pid : -1,
                     path,
                     argv,
                     envp);

    char abs_path[PATH_MAX];
    if (resolve_user_path(path, abs_path, sizeof(abs_path)) != 0)
    {
        return -1;
    }
    char args[EXEC_MAX_ARGS][EXEC_MAX_ARG_LEN];
    int argc = copy_in_args(argv, args);
    if (argc < 0)
        return -1;
    if (argc == 0)
    {
        path_safe_copy(args[0], EXEC_MAX_ARG_LEN, abs_path);
        argc = 1;
    }

    TEST_SYSCALL_LOG("sys_execve: pid=%d path=%s argc=%d argv_ptr=%p first_arg=%s\n",
                     current_process ? current_process->pid : -1,
                     abs_path,
                     argc,
                     argv,
                     (argc > 0) ? args[0] : "<none>");

    pml4_t old_pml4 = current_process->pml4;
    pml4_t new_pml4 = vmm_new_pml4();
    if (!new_pml4)
    {
        TEST_SYSCALL_LOG("sys_execve: pid=%d new_pml4 alloc failed path=%s\n",
                         current_process ? current_process->pid : -1,
                         abs_path);
        return -1;
    }

    uint64_t entry_point;
    uint64_t max_vaddr;
    if (!elf_load(abs_path, &entry_point, &max_vaddr, new_pml4))
    {
        TEST_SYSCALL_LOG("sys_execve: pid=%d elf_load failed path=%s\n",
                         current_process ? current_process->pid : -1,
                         abs_path);
        vmm_destroy_pml4(new_pml4);
        return -1;
    }

    constexpr uint64_t stack_pages = 4;
    constexpr uint64_t guard_pages = 1;
    constexpr uint64_t stack_top = 0x7FFFFFFFF000;
    constexpr uint64_t stack_size = stack_pages * PAGE_SIZE;
    constexpr uint64_t guard_size = guard_pages * PAGE_SIZE;
    constexpr uint64_t total_size = stack_size + guard_size;
    constexpr uint64_t guard_start = stack_top - total_size;
    constexpr uint64_t stack_base = guard_start + guard_size;

    list_item_t old_vm_areas;
    uint32_t old_vm_area_count = 0;
    vm_area_detach(current_process, &old_vm_areas, &old_vm_area_count);

    constexpr uint32_t stack_vma_flags = VMA_READ | VMA_WRITE | VMA_USER | VMA_STACK | VMA_ANON;
    if (!map_user_anonymous_range(current_process, new_pml4, stack_base, stack_size, stack_vma_flags))
    {
        TEST_SYSCALL_LOG("sys_execve: pid=%d stack alloc failed path=%s\n",
                         current_process ? current_process->pid : -1,
                         abs_path);
        vm_area_restore(current_process, &old_vm_areas, old_vm_area_count);
        vmm_destroy_pml4(new_pml4);
        return -1;
    }

    constexpr uint32_t guard_vma_flags = VMA_USER | VMA_STACK | VMA_ANON;
    if (!vm_area_add(current_process, guard_start, stack_base, guard_vma_flags))
    {
        TEST_SYSCALL_LOG("sys_execve: pid=%d guard alloc failed path=%s\n",
                         current_process ? current_process->pid : -1,
                         abs_path);
        vm_area_clear(current_process);
        vm_area_restore(current_process, &old_vm_areas, old_vm_area_count);
        vmm_destroy_pml4(new_pml4);
        return -1;
    }
    vm_area_free_list(&old_vm_areas);

    uint64_t user_rsp = stack_top;
    current_process->pml4 = new_pml4;
    vmm_switch_pml4(new_pml4);
    setup_user_stack(stack_top, args, argc, &user_rsp);

    current_process->heap_end = max_vaddr;
    set_process_name_from_path(current_process, abs_path);
    signal_reset_exec(current_process);
    regs->rcx = entry_point;
    get_cpu()->user_rsp = user_rsp;
    if (current_thread)
    {
        current_thread->user_stack = user_rsp;
        current_thread->saved_user_rsp = user_rsp;
        current_thread->user_stack_base = guard_start;
        current_thread->user_stack_top = stack_top;
        current_thread->fs_base = 0;
        wrfsbase(0);
    }

    if (old_pml4 && old_pml4 != new_pml4)
    {
        vmm_destroy_pml4(old_pml4);
    }

    TEST_SYSCALL_LOG("sys_execve: pid=%d entry=%lx rsp=%lx\n",
                     current_process ? current_process->pid : -1,
                     (unsigned long)entry_point,
                     (unsigned long)user_rsp);
    return 0;
}
