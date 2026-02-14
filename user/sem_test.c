#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>

static sem_t g_sem;
static volatile int g_state = 0;
static volatile int g_waiter_ready = 0;
static volatile int g_waiter_done = 0;

static int spin_wait_state(volatile int *state, int expected, int max_iters)
{
    for (int i = 0; i < max_iters; i++)
    {
        if (__atomic_load_n(state, __ATOMIC_ACQUIRE) == expected)
            return 1;
        yield();
    }
    return 0;
}

static int spin_wait_at_least(volatile int *value, int expected, int max_iters)
{
    for (int i = 0; i < max_iters; i++)
    {
        if (__atomic_load_n(value, __ATOMIC_ACQUIRE) >= expected)
            return 1;
        yield();
    }
    return 0;
}

static void* sem_fast_waiter(void* arg)
{
    (void)arg;
    sem_wait(&g_sem);
    __atomic_store_n(&g_state, 1, __ATOMIC_RELEASE);
    return nullptr;
}

static void* sem_blocking_waiter(void* arg)
{
    (void)arg;
    __atomic_store_n(&g_state, 1, __ATOMIC_RELEASE);
    sem_wait(&g_sem);
    __atomic_store_n(&g_state, 2, __ATOMIC_RELEASE);
    return nullptr;
}

static void* sem_multi_waiter(void* arg)
{
    (void)arg;
    __atomic_fetch_add(&g_waiter_ready, 1, __ATOMIC_RELEASE);
    sem_wait(&g_sem);
    __atomic_fetch_add(&g_waiter_done, 1, __ATOMIC_RELEASE);
    return nullptr;
}

int main(void)
{
    constexpr int k_spin_limit = 20000;

    sem_init(&g_sem, 1);
    g_state = 0;
    pthread_t fast_thread;
    if (pthread_create(&fast_thread, nullptr, sem_fast_waiter, nullptr) != 0)
        return 1;
    if (!spin_wait_state(&g_state, 1, k_spin_limit))
        return 2;
    if (pthread_join(fast_thread, nullptr) != 0)
        return 3;

    sem_init(&g_sem, 0);
    g_state = 0;
    pthread_t block_thread;
    if (pthread_create(&block_thread, nullptr, sem_blocking_waiter, nullptr) != 0)
        return 4;
    if (!spin_wait_state(&g_state, 1, k_spin_limit))
        return 5;
    for (int i = 0; i < 1000; i++)
    {
        if (__atomic_load_n(&g_state, __ATOMIC_ACQUIRE) != 1)
            return 6;
        yield();
    }
    sem_post(&g_sem);
    if (!spin_wait_state(&g_state, 2, k_spin_limit))
        return 7;
    if (pthread_join(block_thread, nullptr) != 0)
        return 8;

    sem_init(&g_sem, 0);
    g_waiter_ready = 0;
    g_waiter_done = 0;
    pthread_t waiter_a;
    pthread_t waiter_b;
    if (pthread_create(&waiter_a, nullptr, sem_multi_waiter, nullptr) != 0)
        return 9;
    if (pthread_create(&waiter_b, nullptr, sem_multi_waiter, nullptr) != 0)
        return 10;
    if (!spin_wait_at_least(&g_waiter_ready, 2, k_spin_limit))
        return 11;
    if (__atomic_load_n(&g_waiter_done, __ATOMIC_ACQUIRE) != 0)
        return 12;
    sem_post(&g_sem);
    sem_post(&g_sem);
    if (!spin_wait_at_least(&g_waiter_done, 2, k_spin_limit))
        return 13;
    if (pthread_join(waiter_a, nullptr) != 0)
        return 14;
    if (pthread_join(waiter_b, nullptr) != 0)
        return 15;

    return 0;
}
