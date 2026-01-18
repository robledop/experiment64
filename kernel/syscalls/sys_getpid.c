#include <task/process.h>

int sys_getpid(void)
{
    return current_process->pid;
}
