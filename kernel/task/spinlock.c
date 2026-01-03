#include <spinlock.h>

#ifdef TEST_MODE
#include <string.h>
#include <test.h>
#include <debug.h>
extern spinlock_t scheduler_lock;
#endif

void spinlock_init(spinlock_t *lock)
{
    lock->locked = false;
}

void spinlock_acquire(spinlock_t *lock)
{
#ifdef TEST_MODE
    uint32_t spin = 0;
    bool watching = false;
    if (lock == &scheduler_lock && g_current_test_name)
    {
        const char *tn = (const char *)g_current_test_name;
        if (strcmp(tn, "test_scheduler") == 0 || strcmp(tn, "test_syscall_execve") == 0 ||
            strcmp(tn, "test_spinlock_contention") == 0)
            watching = true;
    }
#endif
    while (__atomic_test_and_set(&lock->locked, __ATOMIC_ACQUIRE))
    {
        while (lock->locked)
        {
#ifdef TEST_MODE
            if (watching)
            {
                // If we spin "too long" here, the system is effectively deadlocked because
                // callers often disable interrupts before acquiring scheduler_lock.
                if (++spin == 10000000u)
                {
                    panic("scheduler_lock deadlock");
                }
            }
#endif
            __asm__ volatile("pause");
        }
    }
}

void spinlock_release(spinlock_t *lock)
{
    __atomic_clear(&lock->locked, __ATOMIC_RELEASE);
}
