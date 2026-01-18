#include <lib/string.h>
#include <sys/syscall.h>

int sys_execve(const char* path, const char* const argv[], const char* const envp[], struct syscall_regs* regs);

int sys_exec(const char* path, struct syscall_regs* regs)
{
    // Add a null terminator so copy_in_args stops after the single path entry.
    const char* argv[2] = {path, nullptr};
    return sys_execve(path, argv, nullptr, regs);
}
