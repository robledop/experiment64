#include <syscall_common.h>
#include <lib/elf.h>
#include <lib/string.h>
#include <mem/vmm.h>
#include <mem/pmm.h>

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
        "iretq\n"
        :
        : "r"(user_ss), "r"(stack), "r"(rflags), "r"(user_cs), "r"(entry)
        : "memory");
}

/**
 * @brief Write a uint64_t value to a virtual address in another process's address space.
 *
 * Translates the virtual address to physical via the target's PML4, then writes
 * through the HHDM. Used to construct the initial stack in a spawned process.
 *
 * @param pml4 Page table root of the target process.
 * @param vaddr Virtual address to write to (must be mapped).
 * @param val   Value to write.
 */
static void write_to_foreign(pml4_t pml4, uint64_t vaddr, uint64_t val)
{
    uint64_t phys = vmm_virt_to_phys(pml4, vaddr);
    uint64_t offset = vaddr & (PAGE_SIZE - 1);
    uint64_t *ptr = (uint64_t *)((phys & ~(PAGE_SIZE - 1)) + g_hhdm_offset + offset);
    *ptr = val;
}

/**
 * @brief Set up the initial stack for a spawned process via HHDM writes.
 *
 * Constructs the SysV x86_64 ABI initial stack layout in the target process's
 * address space. Since the spawned process uses a different PML4, all writes
 * go through HHDM using physical address translation.
 *
 * Stack layout (top to bottom):
 *   auxv entries (AT_NULL terminated)
 *   envp[] = { NULL }
 *   argv[] = { NULL }  (spawn has no args)
 *   argc = 0
 *
 * @param pml4     Page table root of the target process.
 * @param stack_top Top of the user stack.
 * @param elf_info ELF load result for auxiliary vector entries.
 * @return The final stack pointer value.
 */
static uint64_t setup_spawn_stack(pml4_t pml4, uint64_t stack_top, const elf_load_result_t *elf_info)
{
    uint64_t sp = stack_top;

    /* Align to 16 bytes */
    sp &= ~0xFul;

    /* Auxiliary vector (pushed bottom-up, AT_NULL first) */
    sp -= sizeof(uint64_t);
    write_to_foreign(pml4, sp, 0); /* AT_NULL value */
    sp -= sizeof(uint64_t);
    write_to_foreign(pml4, sp, AT_NULL);

    sp -= sizeof(uint64_t);
    write_to_foreign(pml4, sp, PAGE_SIZE);
    sp -= sizeof(uint64_t);
    write_to_foreign(pml4, sp, AT_PAGESZ);

    sp -= sizeof(uint64_t);
    write_to_foreign(pml4, sp, elf_info->interp_base);
    sp -= sizeof(uint64_t);
    write_to_foreign(pml4, sp, AT_BASE);

    sp -= sizeof(uint64_t);
    write_to_foreign(pml4, sp, elf_info->entry);
    sp -= sizeof(uint64_t);
    write_to_foreign(pml4, sp, AT_ENTRY);

    sp -= sizeof(uint64_t);
    write_to_foreign(pml4, sp, elf_info->phnum);
    sp -= sizeof(uint64_t);
    write_to_foreign(pml4, sp, AT_PHNUM);

    sp -= sizeof(uint64_t);
    write_to_foreign(pml4, sp, elf_info->phent);
    sp -= sizeof(uint64_t);
    write_to_foreign(pml4, sp, AT_PHENT);

    sp -= sizeof(uint64_t);
    write_to_foreign(pml4, sp, elf_info->phdr_vaddr);
    sp -= sizeof(uint64_t);
    write_to_foreign(pml4, sp, AT_PHDR);

    /* envp[] = { NULL } */
    sp -= sizeof(uint64_t);
    write_to_foreign(pml4, sp, 0);

    /* argv[] = { NULL } (spawn has no arguments) */
    sp -= sizeof(uint64_t);
    write_to_foreign(pml4, sp, 0);

    /* argc = 0 */
    sp -= sizeof(uint64_t);
    write_to_foreign(pml4, sp, 0);

    return sp;
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

    elf_load_result_t elf_result;
    if (!elf_load_ex(abs_path, &elf_result, new_pml4))
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

    process_t* proc = process_create(abs_path);
    if (!proc)
    {
        vmm_destroy_pml4(new_pml4);
        return -1;
    }
    set_process_name_from_path(proc, abs_path);
    proc->pml4 = new_pml4;
    proc->parent = current_process;
    proc->heap_end = elf_result.max_vaddr;

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

    /* Set up the initial stack with auxv in the new process's address space */
    uint64_t user_rsp = setup_spawn_stack(new_pml4, stack_top, &elf_result);

    /* If dynamic: enter the interpreter instead of the program directly */
    uint64_t entry = elf_result.interp_entry ? elf_result.interp_entry : elf_result.entry;

    thread_t* thread = thread_create(proc, spawn_trampoline, true);
    if (!thread)
    {
        process_destroy(proc);
        return -1;
    }
    thread->user_entry = entry;
    thread->user_stack = user_rsp;
    thread->user_stack_base = guard_start;
    thread->user_stack_top = stack_top;
    thread_make_ready(thread);

    TEST_SYSCALL_LOG("sys_spawn: created pid=%d tid=%d entry=%lx stack=%lx parent=%d\n",
                     proc->pid,
                     thread ? thread->tid : -1,
                     (unsigned long)entry,
                     (unsigned long)stack_top,
                     current_process ? current_process->pid : -1);
    return proc->pid;
}
