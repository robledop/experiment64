#include <sys/syscall.h>
#include <sys/wait.h>
#include <syscall_common.h>

// Like sys_waitpid but additionally returns crash_info for the reaped process.
int sys_wait4(int pid, int *status, int options, crash_info_t *info)
{
    return wait_for_child(pid, status, options, info, "sys_wait4");
}
