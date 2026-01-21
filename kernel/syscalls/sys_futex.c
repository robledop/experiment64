#include <sys/syscall.h>
#include <syscall_common.h>
#include <task/process.h>

static bool futex_addr_ok(const uint32_t* uaddr, const char* op)
{
    if (!uaddr)
        return false;
    if (((uintptr_t)uaddr & (sizeof(uint32_t) - 1)) != 0)
        return false;
    if (!user_ptr_read_ok(uaddr, sizeof(uint32_t), op))
        return false;
    return true;
}

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

int sys_futex_wake(uint32_t* uaddr, int count)
{
    if (!futex_addr_ok(uaddr, "sys_futex_wake"))
        return -1;
    if (count <= 0)
        return 0;
    return thread_wakeup_n(uaddr, current_process, count);
}
