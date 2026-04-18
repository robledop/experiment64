#include <syscall_common.h>

int sys_wait(int *status)
{
#ifdef TEST_MODE
    printk("sys_wait: pid=%d waiting...\n", current_process ? current_process->pid : -1);
#endif
    return wait_for_child(-1, status, 0, nullptr, "sys_wait");
}
