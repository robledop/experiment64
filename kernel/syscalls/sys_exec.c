#include <arch/x86_64/cpu.h>
#include <syscall_common.h>
#include <sys/syscall.h>

int sys_exec(const char *path, struct syscall_regs *regs)
{
    if (!path)
        return -1;

    cpu_t *cpu = get_cpu();
    if (!cpu || cpu->user_rsp == 0)
        return -1;

    uint64_t argv_vals[2] = {(uint64_t)path, 0};
    uint64_t argv_addr = cpu->user_rsp - sizeof(argv_vals);
    if (!copy_to_user((void *)argv_addr, argv_vals, sizeof(argv_vals)))
        return -1;

    return sys_execve(path, (const char *const *)argv_addr, nullptr, regs);
}
