#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>

// Minimal semaphore behavior checks: fast path, blocking, and multi-waiter.
static sem_t g_sem;
static volatile int g_state        = 0;
static volatile int g_waiter_ready = 0;
static volatile int g_waiter_done  = 0;

// Spin with acquire loads and yield to avoid hard busy-waiting.
static int spin_wait_state(const volatile int *state, int expected, int max_iters)
{
    for (int i = 0; i < max_iters; i++) {
        if (__atomic_load_n(state, __ATOMIC_ACQUIRE) == expected)
            return 1;
        yield();
    }
    return 0;
}

// Spin until a shared counter reaches the required minimum.
static int spin_wait_at_least(const volatile int *value, int expected, int max_iters)
{
    for (int i = 0; i < max_iters; i++) {
        if (__atomic_load_n(value, __ATOMIC_ACQUIRE) >= expected)
            return 1;
        yield();
    }
    return 0;
}

// Wait on the semaphore and mark completion for the fast-path test.
static void *sem_fast_waiter(void *arg)
{
    (void)arg;
    sem_wait(&g_sem);
    __atomic_store_n(&g_state, 1, __ATOMIC_RELEASE);
    return nullptr;
}

// Mark "ready", then block on the semaphore, then mark "done".
static void *sem_blocking_waiter(void *arg)
{
    (void)arg;
    __atomic_store_n(&g_state, 1, __ATOMIC_RELEASE);
    sem_wait(&g_sem);
    __atomic_store_n(&g_state, 2, __ATOMIC_RELEASE);
    return nullptr;
}

// Used for the multi-waiter test: track ready and done counts.
static void *sem_multi_waiter(void *arg)
{
    (void)arg;
    __atomic_fetch_add(&g_waiter_ready, 1, __ATOMIC_RELEASE);
    sem_wait(&g_sem);
    __atomic_fetch_add(&g_waiter_done, 1, __ATOMIC_RELEASE);
    return nullptr;
}

static int expect_sem_error(const int rc, const int expected_errno)
{
    return rc == -1 && errno == expected_errno;
}

int main(void)
{
    constexpr int k_spin_limit = 20000;

    // Test 1: semaphore with initial count, waiter should pass immediately.
    if (sem_init(&g_sem, 1) != 0)
        return 1;
    g_state = 0;
    pthread_t fast_thread;
    if (pthread_create(&fast_thread, nullptr, sem_fast_waiter, nullptr) != 0)
        return 2;
    if (!spin_wait_state(&g_state, 1, k_spin_limit))
        return 3;
    if (pthread_join(fast_thread, nullptr) != 0)
        return 4;
    if (sem_destroy(&g_sem) != 0)
        return 5;

    // Test 2: semaphore starts at 0, waiter blocks until post.
    if (sem_init(&g_sem, 0) != 0)
        return 6;
    g_state = 0;
    pthread_t block_thread;
    if (pthread_create(&block_thread, nullptr, sem_blocking_waiter, nullptr) != 0)
        return 7;
    if (!spin_wait_state(&g_state, 1, k_spin_limit))
        return 8;
    for (int i = 0; i < 1000; i++) {
        if (__atomic_load_n(&g_state, __ATOMIC_ACQUIRE) != 1)
            return 9;
        yield();
    }
    if (sem_post(&g_sem) != 0)
        return 10;
    if (!spin_wait_state(&g_state, 2, k_spin_limit))
        return 11;
    if (pthread_join(block_thread, nullptr) != 0)
        return 12;
    if (sem_destroy(&g_sem) != 0)
        return 13;

    // Test 3: two waiters block, then both are released by two posts.
    if (sem_init(&g_sem, 0) != 0)
        return 14;
    g_waiter_ready = 0;
    g_waiter_done  = 0;
    pthread_t waiter_a;
    pthread_t waiter_b;
    if (pthread_create(&waiter_a, nullptr, sem_multi_waiter, nullptr) != 0)
        return 15;
    if (pthread_create(&waiter_b, nullptr, sem_multi_waiter, nullptr) != 0)
        return 16;
    if (!spin_wait_at_least(&g_waiter_ready, 2, k_spin_limit))
        return 17;
    if (__atomic_load_n(&g_waiter_done, __ATOMIC_ACQUIRE) != 0)
        return 18;
    if (sem_post(&g_sem) != 0)
        return 19;
    if (sem_post(&g_sem) != 0)
        return 20;
    if (!spin_wait_at_least(&g_waiter_done, 2, k_spin_limit))
        return 21;
    if (pthread_join(waiter_a, nullptr) != 0)
        return 22;
    if (pthread_join(waiter_b, nullptr) != 0)
        return 23;
    if (sem_destroy(&g_sem) != 0)
        return 24;

    // Test 4: API error paths follow POSIX style (-1 + errno).
    errno = 0;
    if (!expect_sem_error(sem_init(nullptr, 0), EINVAL))
        return 25;

    errno = 0;
    if (!expect_sem_error(sem_init(&g_sem, -1), EINVAL))
        return 26;

    errno = 0;
    if (!expect_sem_error(sem_wait(nullptr), EINVAL))
        return 27;

    errno = 0;
    if (!expect_sem_error(sem_post(nullptr), EINVAL))
        return 28;

    errno = 0;
    if (!expect_sem_error(sem_destroy(nullptr), EINVAL))
        return 29;

    if (sem_init(&g_sem, 0) != 0)
        return 30;
    if (pthread_mutex_lock(&g_sem.lock) != 0)
        return 31;
    errno = 0;
    if (!expect_sem_error(sem_destroy(&g_sem), EBUSY))
        return 32;
    if (pthread_mutex_unlock(&g_sem.lock) != 0)
        return 33;
    if (sem_destroy(&g_sem) != 0)
        return 34;

    if (sem_init(&g_sem, INT_MAX) != 0)
        return 35;
    errno = 0;
    if (!expect_sem_error(sem_post(&g_sem), EAGAIN))
        return 36;
    if (sem_destroy(&g_sem) != 0)
        return 37;

    return 0;
}
