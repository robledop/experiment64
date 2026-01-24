#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <limits.h>

struct pthread_start
{
    void* (*start)(void*);
    void* arg;
};

struct pthread_ret_entry
{
    void* value;
    int tid;
    bool used;
};

#define PTHREAD_RET_MAX 128

static pthread_mutex_t g_ret_lock = PTHREAD_MUTEX_INITIALIZER;
static struct pthread_ret_entry g_ret_table[PTHREAD_RET_MAX];

struct pthread_detached_entry
{
    int tid;
    bool used;
};

#define PTHREAD_DETACHED_MAX 128

static struct pthread_detached_entry g_detached_table[PTHREAD_DETACHED_MAX];

int pthread_mutex_init(pthread_mutex_t* mutex, const void* attr)
{
    (void)attr;
    if (!mutex)
        return -1;
    __atomic_store_n(&mutex->__state, 0, __ATOMIC_RELAXED);
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t* mutex)
{
    if (!mutex)
        return -1;
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t* mutex)
{
    if (!mutex)
        return -1;

    for (;;)
    {
        int expected = 0;
        if (__atomic_compare_exchange_n(&mutex->__state, &expected, 1, false,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            return 0;
        futex_wait(&mutex->__state, 1);
    }
}

int pthread_mutex_trylock(pthread_mutex_t* mutex)
{
    if (!mutex)
        return -1;

    int expected = 0;
    if (__atomic_compare_exchange_n(&mutex->__state, &expected, 1, false,
                                    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        return 0;
    return -1;
}

int pthread_mutex_unlock(pthread_mutex_t* mutex)
{
    if (!mutex)
        return -1;

    __atomic_store_n(&mutex->__state, 0, __ATOMIC_RELEASE);
    futex_wake(&mutex->__state, 1);
    return 0;
}

int pthread_cond_init(pthread_cond_t* cond, const void* attr)
{
    (void)attr;
    if (!cond)
        return -1;
    __atomic_store_n(&cond->__seq, 0, __ATOMIC_RELAXED);
    return 0;
}

int pthread_cond_destroy(pthread_cond_t* cond)
{
    if (!cond)
        return -1;
    return 0;
}

int pthread_cond_wait(pthread_cond_t* cond, pthread_mutex_t* mutex)
{
    if (!cond || !mutex)
        return -1;

    int seq = __atomic_load_n(&cond->__seq, __ATOMIC_RELAXED);
    pthread_mutex_unlock(mutex);
    futex_wait(&cond->__seq, seq);
    pthread_mutex_lock(mutex);
    return 0;
}

int pthread_cond_signal(pthread_cond_t* cond)
{
    if (!cond)
        return -1;

    __atomic_fetch_add(&cond->__seq, 1, __ATOMIC_RELEASE);
    futex_wake(&cond->__seq, 1);
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t* cond)
{
    if (!cond)
        return -1;

    __atomic_fetch_add(&cond->__seq, 1, __ATOMIC_RELEASE);
    futex_wake(&cond->__seq, INT_MAX);
    return 0;
}

int pthread_once(pthread_once_t* once_control, void (*init_routine)(void))
{
    if (!once_control || !init_routine)
        return -1;

    int state = __atomic_load_n(&once_control->__state, __ATOMIC_ACQUIRE);
    if (state == 2)
        return 0;

    int expected = 0;
    if (__atomic_compare_exchange_n(&once_control->__state, &expected, 1, false,
                                    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
    {
        init_routine();
        __atomic_store_n(&once_control->__state, 2, __ATOMIC_RELEASE);
        futex_wake(&once_control->__state, INT_MAX);
        return 0;
    }

    while (__atomic_load_n(&once_control->__state, __ATOMIC_ACQUIRE) != 2)
        futex_wait(&once_control->__state, 1);
    return 0;
}

static bool pthread_detached_add_locked(int tid)
{
    int free_slot = -1;
    for (int i = 0; i < PTHREAD_DETACHED_MAX; i++)
    {
        if (g_detached_table[i].used && g_detached_table[i].tid == tid)
            return false;
        if (!g_detached_table[i].used && free_slot < 0)
            free_slot = i;
    }
    if (free_slot < 0)
        return false;

    g_detached_table[free_slot].used = true;
    g_detached_table[free_slot].tid = tid;
    return true;
}

static bool pthread_detached_remove_locked(int tid)
{
    for (int i = 0; i < PTHREAD_DETACHED_MAX; i++)
    {
        if (g_detached_table[i].used && g_detached_table[i].tid == tid)
        {
            g_detached_table[i].used = false;
            g_detached_table[i].tid = 0;
            return true;
        }
    }
    return false;
}

static bool pthread_ret_drop_locked(int tid)
{
    for (int i = 0; i < PTHREAD_RET_MAX; i++)
    {
        if (g_ret_table[i].used && g_ret_table[i].tid == tid)
        {
            g_ret_table[i].used = false;
            g_ret_table[i].tid = 0;
            g_ret_table[i].value = nullptr;
            return true;
        }
    }
    return false;
}

static void pthread_ret_store(int tid, void* value)
{
    pthread_mutex_lock(&g_ret_lock);

    if (pthread_detached_remove_locked(tid))
    {
        pthread_mutex_unlock(&g_ret_lock);
        return;
    }

    int free_slot = -1;
    for (int i = 0; i < PTHREAD_RET_MAX; i++)
    {
        if (g_ret_table[i].used && g_ret_table[i].tid == tid)
        {
            g_ret_table[i].value = value;
            pthread_mutex_unlock(&g_ret_lock);
            return;
        }
        if (!g_ret_table[i].used && free_slot < 0)
            free_slot = i;
    }

    if (free_slot >= 0)
    {
        g_ret_table[free_slot].used = true;
        g_ret_table[free_slot].tid = tid;
        g_ret_table[free_slot].value = value;
    }

    pthread_mutex_unlock(&g_ret_lock);
}

static void* pthread_ret_take(int tid)
{
    void* value = nullptr;

    pthread_mutex_lock(&g_ret_lock);
    for (int i = 0; i < PTHREAD_RET_MAX; i++)
    {
        if (g_ret_table[i].used && g_ret_table[i].tid == tid)
        {
            value = g_ret_table[i].value;
            g_ret_table[i].used = false;
            g_ret_table[i].tid = 0;
            g_ret_table[i].value = nullptr;
            break;
        }
    }
    pthread_mutex_unlock(&g_ret_lock);
    return value;
}

static void pthread_trampoline(void* arg)
{
    struct pthread_start* start = (struct pthread_start*)arg;
    void* ret = nullptr;
    if (start && start->start)
        ret = start->start(start->arg);
    free(start);
    pthread_ret_store(gettid(), ret);
    thread_exit(0);
}

int pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                   void* (*start_routine)(void*), void* arg)
{
    (void)attr;
    if (!thread || !start_routine)
        return -1;

    struct pthread_start* start = malloc(sizeof(*start));
    if (!start)
        return -1;

    start->start = start_routine;
    start->arg = arg;

    int tid = thread_create(pthread_trampoline, start);
    if (tid < 0)
    {
        free(start);
        return -1;
    }

    *thread = tid;
    return 0;
}

[[noreturn]] void pthread_exit(void* retval)
{
    pthread_ret_store(gettid(), retval);
    thread_exit(0);
    __builtin_unreachable();
}

int pthread_join(pthread_t thread, void** retval)
{
    int status = 0;
    if (thread_join(thread, &status) != 0)
        return -1;

    if (retval)
        *retval = pthread_ret_take(thread);
    return 0;
}

int pthread_detach(pthread_t thread)
{
    if (thread <= 0)
        return -1;

    if (thread_detach(thread) != 0)
        return -1;

    pthread_mutex_lock(&g_ret_lock);
    bool had_ret = pthread_ret_drop_locked(thread);
    if (!had_ret)
    {
        if (!pthread_detached_add_locked(thread))
        {
            pthread_mutex_unlock(&g_ret_lock);
            return -1;
        }
    }
    pthread_mutex_unlock(&g_ret_lock);

    return 0;
}

pthread_t pthread_self(void)
{
    return gettid();
}
