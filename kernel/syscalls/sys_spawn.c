#include "syscall_common.h"

#include <lib/elf.h>
#include <mem/pmm.h>
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
    if (!path || !*path)
        return -1;
    TEST_SYSCALL_LOG("sys_spawn: parent pid=%d path=%s\n", current_process ? current_process->pid : -1, path);
    char abs_path[PATH_MAX];
    if (resolve_user_path(path, abs_path, sizeof(abs_path)) != 0)
        // ReSharper disable once CppDFAUnreachableCode
        return -1;

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

    uint64_t stack_top = 0x7FFFFFFFF000;
    uint64_t stack_size = 4 * 4096;
    uint64_t stack_base = stack_top - stack_size;

    for (uint64_t addr = stack_base; addr < stack_top; addr += 4096)
    {
        void* phys = pmm_alloc_page();
        if (!phys)
        {
            TEST_SYSCALL_LOG("sys_spawn: pid=%d stack alloc failed path=%s\n",
                             current_process ? current_process->pid : -1, abs_path);
            vmm_destroy_pml4(new_pml4);
            return -1;
        }
        vmm_map_page(new_pml4, addr, (uint64_t)phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
    }

    process_t* proc = process_create(path);
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
    vm_area_add(proc, stack_base, stack_top, VMA_READ | VMA_WRITE | VMA_USER | VMA_STACK);

    thread_t* thread = thread_create(proc, spawn_trampoline, true);
    if (!thread)
    {
        process_destroy(proc);
        return -1;
    }
    thread->user_entry = entry_point;
    thread->user_stack = stack_top;

    TEST_SYSCALL_LOG("sys_spawn: created pid=%d tid=%d entry=%lx stack=%lx parent=%d\n",
                     proc->pid,
                     thread ? thread->tid : -1,
                     (unsigned long)entry_point,
                     (unsigned long)stack_top,
                     current_process ? current_process->pid : -1);
    return proc->pid;
}
