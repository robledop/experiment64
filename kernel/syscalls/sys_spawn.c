#include <syscall_common.h>

#include <lib/elf.h>
#include <mem/vmm.h>

static void spawn_trampoline(void)
{
    uint64_t user_cs = 0x20 | 3;
    uint64_t user_ss = 0x18 | 3;
    uint64_t rflags = 0x202;

    uint64_t stack = current_thread->user_stack;
    uint64_t entry = current_thread->user_entry;

    __asm__ volatile(
        "cli\n"
        "swapgs\n"
        "mov ds, %0\n"
        "mov es, %0\n"
        "mov fs, %0\n"
        "mov gs, %0\n"
        "push %0\n" // SS
        "push %1\n" // RSP
        "push %2\n" // RFLAGS
        "push %3\n" // CS
        "push %4\n" // RIP
        "xor rdi, rdi\n" // argc = 0
        "xor rsi, rsi\n" // argv = nullptr
        "iretq\n"
        :
        : "r"(user_ss), "r"(stack), "r"(rflags), "r"(user_cs), "r"(entry)
        : "memory", "rdi", "rsi");
}

int sys_spawn(const char* path)
{
    if (!path)
        return -1;
    TEST_SYSCALL_LOG("sys_spawn: parent pid=%d path_ptr=%p\n", current_process ? current_process->pid : -1, path);
    char abs_path[PATH_MAX];
    if (resolve_user_path(path, abs_path, sizeof(abs_path)) != 0)
        // ReSharper disable once CppDFAUnreachableCode
        return -1;
    TEST_SYSCALL_LOG("sys_spawn: parent pid=%d path=%s\n", current_process ? current_process->pid : -1, abs_path);

    pml4_t new_pml4 = vmm_new_pml4();
    if (!new_pml4)
    {
        TEST_SYSCALL_LOG("sys_spawn: pid=%d new_pml4 alloc failed\n", current_process ? current_process->pid : -1);
        return -1;
    }

    uint64_t entry_point;
    uint64_t max_vaddr;
    if (!elf_load(abs_path, &entry_point, &max_vaddr, new_pml4))
    {
        TEST_SYSCALL_LOG("sys_spawn: pid=%d elf_load failed path=%s\n", current_process ? current_process->pid : -1,
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
    constexpr uint64_t user_rsp = stack_top - 16;

    process_t* proc = process_create(abs_path);
    if (!proc)
    {
        vmm_destroy_pml4(new_pml4);
        return -1;
    }
    set_process_name_from_path(proc, abs_path);
    proc->pml4 = new_pml4;
    proc->parent = current_process;
    proc->heap_end = max_vaddr;

    process_copy_fds(proc, current_process);
    constexpr uint32_t stack_vma_flags = VMA_READ | VMA_WRITE | VMA_USER | VMA_STACK | VMA_ANON;
    if (!map_user_anonymous_range(proc, proc->pml4, stack_base, stack_size, stack_vma_flags))
    {
        TEST_SYSCALL_LOG("sys_spawn: pid=%d stack alloc failed path=%s\n",
                         current_process ? current_process->pid : -1, abs_path);
        process_destroy(proc);
        return -1;
    }

    constexpr uint32_t guard_vma_flags = VMA_USER | VMA_STACK | VMA_ANON;
    if (!vm_area_add(proc, guard_start, stack_base, guard_vma_flags))
    {
        TEST_SYSCALL_LOG("sys_spawn: pid=%d guard alloc failed path=%s\n",
                         current_process ? current_process->pid : -1, abs_path);
        process_destroy(proc);
        return -1;
    }

    thread_t* thread = thread_create(proc, spawn_trampoline, true);
    if (!thread)
    {
        process_destroy(proc);
        return -1;
    }
    thread->user_entry = entry_point;
    thread->user_stack = user_rsp;
    thread->user_stack_base = guard_start;
    thread->user_stack_top = stack_top;
    thread_make_ready(thread);

    TEST_SYSCALL_LOG("sys_spawn: created pid=%d tid=%d entry=%lx stack=%lx parent=%d\n",
                     proc->pid,
                     thread ? thread->tid : -1,
                     (unsigned long)entry_point,
                     (unsigned long)stack_top,
                     current_process ? current_process->pid : -1);
    return proc->pid;
}
