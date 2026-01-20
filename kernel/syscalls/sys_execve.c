#include <syscall_common.h>
#include <lib/elf.h>
#include <lib/string.h>
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
        const char* user_arg = argv[count];
        if (!user_arg)
            break;

        size_t len = 0;
        while (len + 1 < EXEC_MAX_ARG_LEN && user_arg[len])
            len++;
        if (len + 1 >= EXEC_MAX_ARG_LEN && user_arg[len])
            return -1; // argument too long

        memcpy(args[count], user_arg, len);
        args[count][len] = '\0';
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

int sys_execve(const char* path, const char* const argv[], [[maybe_unused]] const char* const envp[],
               struct syscall_regs* regs)
{
    if (!path || !*path) return -1;

    TEST_SYSCALL_LOG("sys_execve: enter pid=%d path_ptr=%p argv_ptr=%p envp_ptr=%p\n",
                     current_process ? current_process->pid : -1,
                     path,
                     argv,
                     envp);

    char abs_path[PATH_MAX];
    if (resolve_user_path(path, abs_path, sizeof(abs_path)) != 0)
        // ReSharper disable once CppDFAUnreachableCode
        return -1;
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
                         current_process ? current_process->pid : -1, abs_path);
        return -1;
    }

    uint64_t entry_point;
    uint64_t max_vaddr;
    if (!elf_load(abs_path, &entry_point, &max_vaddr, new_pml4))
    {
        TEST_SYSCALL_LOG("sys_execve: pid=%d elf_load failed path=%s\n", current_process ? current_process->pid : -1,
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
            TEST_SYSCALL_LOG("sys_execve: pid=%d stack alloc failed path=%s\n",
                             current_process ? current_process->pid : -1, abs_path);
            vmm_destroy_pml4(new_pml4);
            return -1;
        }
        vmm_map_page(new_pml4, addr, (uint64_t)phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
    }

    uint64_t user_rsp = stack_top;
    current_process->pml4 = new_pml4;
    vmm_switch_pml4(new_pml4);
    setup_user_stack(stack_top, args, argc, &user_rsp);

    current_process->heap_end = max_vaddr;
    set_process_name_from_path(current_process, abs_path);
    signal_reset_exec(current_process);
    regs->rcx = entry_point;
    get_cpu()->user_rsp = user_rsp;

    if (old_pml4 && old_pml4 != new_pml4)
        vmm_destroy_pml4(old_pml4);

    TEST_SYSCALL_LOG("sys_execve: pid=%d entry=%lx rsp=%lx\n",
                     current_process ? current_process->pid : -1,
                     (unsigned long)entry_point,
                     (unsigned long)user_rsp);
    return 0;
}
