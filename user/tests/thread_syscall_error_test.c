#include <status.h>
#include <unistd.h>

static volatile int g_slow_started;
static volatile int g_slow_release;

static void slow_worker(void *arg)
{
    (void)arg;
    __atomic_store_n(&g_slow_started, 1, __ATOMIC_RELEASE);
    while (__atomic_load_n(&g_slow_release, __ATOMIC_ACQUIRE) == 0)
        yield();
    thread_exit(0);
}

static void fast_worker(void *arg)
{
    (void)arg;
    thread_exit(7);
}

int main(void)
{
    int status = 0;

    if (thread_create((void (*)(void *))0, nullptr) != -EINVAL)
        return 1;

    if (thread_join(-123, &status) != -ESRCH)
        return 2;

    if (thread_detach(-123) != -ESRCH)
        return 3;

    if (thread_join(gettid(), &status) != -EDEADLK)
        return 4;

    g_slow_started = 0;
    g_slow_release = 0;

    int slow = thread_create(slow_worker, nullptr);
    if (slow < 0)
        return 5;

    while (__atomic_load_n(&g_slow_started, __ATOMIC_ACQUIRE) == 0)
        yield();

    if (thread_detach(slow) != 0)
        return 6;

    if (thread_detach(slow) != -EINVAL)
        return 7;

    if (thread_join(slow, &status) != -EINVAL)
        return 8;

    __atomic_store_n(&g_slow_release, 1, __ATOMIC_RELEASE);

    int fast = thread_create(fast_worker, nullptr);
    if (fast < 0)
        return 9;

    if (thread_join(fast, &status) != 0)
        return 10;

    if (status != 7)
        return 11;

    return 0;
}
