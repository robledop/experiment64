#include <sys/syscall.h>
#include <syscall_common.h>
#include <task/process.h>

int sys_futex_wake(uint32_t* uaddr, int count)
{
    if (!futex_addr_ok(uaddr, "sys_futex_wake")) return -1;
    if (count <= 0) return 0;

    return thread_wakeup_n(uaddr, current_process, count);
}
