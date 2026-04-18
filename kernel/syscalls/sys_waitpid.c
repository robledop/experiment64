#include <sys/syscall.h>
#include <sys/wait.h>
#include <syscall_common.h>

int sys_waitpid(int pid, int *status, int options)
{
#ifdef TEST_MODE
    printk("sys_waitpid: pid=%d waiting for %d...\n",
           current_process ? current_process->pid : -1,
           pid);
#endif
    return wait_for_child(pid, status, options, nullptr, "sys_waitpid");
}
