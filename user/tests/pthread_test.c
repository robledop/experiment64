#include <pthread.h>
#include <status.h>
#include <unistd.h>

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t g_destroy_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_destroy_cv = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t g_mutex_destroy_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_mutex_destroy_sync_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_mutex_destroy_sync_cv = PTHREAD_COND_INITIALIZER;
static pthread_barrier_t g_barrier;
static int g_ready = 0;
static int g_once_count = 0;
static int g_destroy_waiter_ready = 0;
static int g_destroy_release = 0;
static int g_mutex_destroy_waiter_ready = 0;
static int g_mutex_destroy_waiter_done = 0;
static int g_barrier_serial_count = 0;
static int g_barrier_wait_count = 0;

static void once_init(void)
{
    g_once_count++;
}

static void *worker_entry(void *arg)
{
    (void)arg;
    pthread_once(&g_once, once_init);
    pthread_mutex_lock(&g_lock);
    g_ready = 1;
    pthread_cond_signal(&g_cv);
    pthread_mutex_unlock(&g_lock);
    return (void *)0x1234;
}

static void *destroy_waiter_entry(void *arg)
{
    (void)arg;

    pthread_mutex_lock(&g_destroy_lock);
    g_destroy_waiter_ready = 1;
    pthread_cond_signal(&g_destroy_cv);
    while (!g_destroy_release)
        pthread_cond_wait(&g_destroy_cv, &g_destroy_lock);
    pthread_mutex_unlock(&g_destroy_lock);
    return nullptr;
}

static void *mutex_destroy_waiter_entry(void *arg)
{
    (void)arg;

    pthread_mutex_lock(&g_mutex_destroy_sync_lock);
    g_mutex_destroy_waiter_ready = 1;
    pthread_cond_signal(&g_mutex_destroy_sync_cv);
    pthread_mutex_unlock(&g_mutex_destroy_sync_lock);

    pthread_mutex_lock(&g_mutex_destroy_lock);
    pthread_mutex_unlock(&g_mutex_destroy_lock);

    pthread_mutex_lock(&g_mutex_destroy_sync_lock);
    g_mutex_destroy_waiter_done = 1;
    pthread_cond_signal(&g_mutex_destroy_sync_cv);
    pthread_mutex_unlock(&g_mutex_destroy_sync_lock);

    return nullptr;
}

static void *barrier_waiter_entry(void *arg)
{
    (void)arg;

    int rc = pthread_barrier_wait(&g_barrier);
    if (rc == PTHREAD_BARRIER_SERIAL_THREAD)
        __atomic_fetch_add(&g_barrier_serial_count, 1, __ATOMIC_RELAXED);
    else if (rc != 0)
        return (void *)1;

    __atomic_fetch_add(&g_barrier_wait_count, 1, __ATOMIC_RELAXED);
    return nullptr;
}

static void *detach_target_entry(void *arg)
{
    (void)arg;
    for (int i = 0; i < 100; i++)
        yield();
    return nullptr;
}

int main(void)
{
    pthread_t thread;
    if (pthread_create(&thread, nullptr, worker_entry, nullptr) != 0)
        return 1;

    pthread_once(&g_once, once_init);
    pthread_mutex_lock(&g_lock);
    while (!g_ready)
        pthread_cond_wait(&g_cv, &g_lock);
    pthread_mutex_unlock(&g_lock);

    void *ret = nullptr;
    if (pthread_join(thread, &ret) != 0)
        return 2;
    if (ret != (void *)0x1234)
        return 3;
    if (g_once_count != 1)
        return 4;

    g_destroy_waiter_ready = 0;
    g_destroy_release = 0;

    pthread_t waiter;
    if (pthread_create(&waiter, nullptr, destroy_waiter_entry, nullptr) != 0)
        return 5;

    if (pthread_mutex_lock(&g_destroy_lock) != 0)
        return 6;
    while (!g_destroy_waiter_ready) {
        if (pthread_cond_wait(&g_destroy_cv, &g_destroy_lock) != 0)
            return 7;
    }
    if (pthread_cond_destroy(&g_destroy_cv) != EBUSY)
        return 8;
    g_destroy_release = 1;
    if (pthread_cond_signal(&g_destroy_cv) != 0)
        return 9;
    if (pthread_mutex_unlock(&g_destroy_lock) != 0)
        return 10;

    if (pthread_join(waiter, nullptr) != 0)
        return 11;
    if (pthread_cond_destroy(&g_destroy_cv) != 0)
        return 12;
    if (pthread_mutex_destroy(&g_destroy_lock) != 0)
        return 13;

    g_mutex_destroy_waiter_ready = 0;
    g_mutex_destroy_waiter_done = 0;

    if (pthread_mutex_lock(&g_mutex_destroy_lock) != 0)
        return 14;

    pthread_t mutex_waiter;
    if (pthread_create(&mutex_waiter, nullptr, mutex_destroy_waiter_entry, nullptr) != 0)
        return 15;

    if (pthread_mutex_lock(&g_mutex_destroy_sync_lock) != 0)
        return 16;
    while (!g_mutex_destroy_waiter_ready) {
        if (pthread_cond_wait(&g_mutex_destroy_sync_cv, &g_mutex_destroy_sync_lock) != 0)
            return 17;
    }

    if (pthread_mutex_destroy(&g_mutex_destroy_lock) != EBUSY)
        return 18;
    if (pthread_mutex_unlock(&g_mutex_destroy_lock) != 0)
        return 19;

    while (!g_mutex_destroy_waiter_done) {
        if (pthread_cond_wait(&g_mutex_destroy_sync_cv, &g_mutex_destroy_sync_lock) != 0)
            return 20;
    }
    if (pthread_mutex_unlock(&g_mutex_destroy_sync_lock) != 0)
        return 21;

    if (pthread_join(mutex_waiter, nullptr) != 0)
        return 22;
    if (pthread_mutex_destroy(&g_mutex_destroy_lock) != 0)
        return 23;
    if (pthread_cond_destroy(&g_mutex_destroy_sync_cv) != 0)
        return 24;
    if (pthread_mutex_destroy(&g_mutex_destroy_sync_lock) != 0)
        return 25;

    if (pthread_join(pthread_self(), nullptr) != EDEADLK)
        return 26;
    if (pthread_join(-123, nullptr) != ESRCH)
        return 27;
    if (pthread_detach(-123) != ESRCH)
        return 28;

    pthread_mutex_t local = PTHREAD_MUTEX_INITIALIZER;
    if (pthread_mutex_lock(&local) != 0)
        return 29;
    if (pthread_mutex_trylock(&local) != EBUSY)
        return 30;
    if (pthread_mutex_unlock(&local) != 0)
        return 31;
    if (pthread_mutex_unlock(&local) != EPERM)
        return 32;
    if (pthread_mutex_destroy(&local) != 0)
        return 33;

    pthread_t det_thread;
    if (pthread_create(&det_thread, nullptr, detach_target_entry, nullptr) != 0)
        return 34;
    if (pthread_detach(det_thread) != 0)
        return 35;
    if (pthread_detach(det_thread) != EINVAL)
        return 36;

    if (pthread_barrier_init(&g_barrier, nullptr, 3) != 0)
        return 37;

    pthread_t barrier_a;
    pthread_t barrier_b;
    if (pthread_create(&barrier_a, nullptr, barrier_waiter_entry, nullptr) != 0)
        return 38;
    if (pthread_create(&barrier_b, nullptr, barrier_waiter_entry, nullptr) != 0)
        return 39;

    int barrier_rc = pthread_barrier_wait(&g_barrier);
    if (barrier_rc == PTHREAD_BARRIER_SERIAL_THREAD)
        __atomic_fetch_add(&g_barrier_serial_count, 1, __ATOMIC_RELAXED);
    else if (barrier_rc != 0)
        return 40;
    __atomic_fetch_add(&g_barrier_wait_count, 1, __ATOMIC_RELAXED);

    if (pthread_join(barrier_a, &ret) != 0)
        return 41;
    if (ret != nullptr)
        return 42;
    if (pthread_join(barrier_b, &ret) != 0)
        return 43;
    if (ret != nullptr)
        return 44;

    if (__atomic_load_n(&g_barrier_serial_count, __ATOMIC_RELAXED) != 1)
        return 45;
    if (__atomic_load_n(&g_barrier_wait_count, __ATOMIC_RELAXED) != 3)
        return 46;

    if (pthread_barrier_destroy(&g_barrier) != 0)
        return 47;

    return 0;
}
