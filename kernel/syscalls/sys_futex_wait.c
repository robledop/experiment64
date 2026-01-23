#include <sys/syscall.h>
#include <syscall_common.h>
#include <task/process.h>

int sys_futex_wait(uint32_t* uaddr, uint32_t expected)
{
    if (!futex_addr_ok(uaddr, "sys_futex_wait"))
        return -1;

    uint64_t rflags;
    SPIN_LOCK_INT_SAVE(scheduler_lock, rflags);

    const uint32_t value = __atomic_load_n(uaddr, __ATOMIC_RELAXED);
    if (value != expected)
    {
        SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
        return -1;
    }

    thread_sleep(uaddr, &scheduler_lock);
    SPIN_UNLOCK_INT_RESTORE(scheduler_lock, rflags);
    return 0;
}