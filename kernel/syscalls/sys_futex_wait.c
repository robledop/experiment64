#include <sys/syscall.h>
#include <syscall_common.h>
#include <task/process.h>

int sys_futex_wait(uint32_t *uaddr, uint32_t expected)
{
    if (!futex_addr_ok(uaddr, "sys_futex_wait"))
        return -1;

    WITH_LOCK(scheduler_lock) {
        const uint32_t value = __atomic_load_n(uaddr, __ATOMIC_RELAXED);
        if (value != expected)
            return -1;

        thread_sleep(uaddr, &scheduler_lock);
    }
    return 0;
}