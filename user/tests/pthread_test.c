#include <pthread.h>

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t g_destroy_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_destroy_cv = PTHREAD_COND_INITIALIZER;
static int g_ready = 0;
static int g_once_count = 0;
static int g_destroy_waiter_ready = 0;
static int g_destroy_release = 0;

static void once_init(void)
{
    g_once_count++;
}

static void* worker_entry(void* arg)
{
    (void)arg;
    pthread_once(&g_once, once_init);
    pthread_mutex_lock(&g_lock);
    g_ready = 1;
    pthread_cond_signal(&g_cv);
    pthread_mutex_unlock(&g_lock);
    return (void*)0x1234;
}

static void* destroy_waiter_entry(void* arg)
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

    void* ret = nullptr;
    if (pthread_join(thread, &ret) != 0)
        return 2;
    if (ret != (void*)0x1234)
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
    if (pthread_cond_destroy(&g_destroy_cv) == 0)
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

    return 0;
}
