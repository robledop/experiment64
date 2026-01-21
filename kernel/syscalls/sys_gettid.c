#include <task/process.h>

int sys_gettid(void)
{
    thread_t* t = current_thread;
    if (!t) return -1;
    return t->tid;
}
