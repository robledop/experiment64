#include <limits.h>
#include <pthread.h>
#include <semaphore.h>
#include <status.h>
#include <stdlib.h>
#include <tls.h>
#include <unistd.h>

struct pthread_start {
    void *(*start)(void *);
    void *arg;
};

struct pthread_ret_entry {
    void *value;
    int tid;
    bool used;
};

#define PTHREAD_RET_MAX 1024
static pthread_mutex_t g_ret_lock = PTHREAD_MUTEX_INITIALIZER;
static struct pthread_ret_entry g_ret_table[PTHREAD_RET_MAX];

struct pthread_detached_entry {
    int tid;
    bool used;
};

#define PTHREAD_DETACHED_MAX 1024
static struct pthread_detached_entry g_detached_table[PTHREAD_DETACHED_MAX];

struct pthread_join_entry {
    int tid;
    bool used;
};

#define PTHREAD_JOIN_MAX 1024
static struct pthread_join_entry g_join_table[PTHREAD_JOIN_MAX];

static thread_local struct pthread_start *g_start_ctx;

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
    (void)attr;
    if (!mutex)
        return EINVAL;

    __atomic_store_n(&mutex->__state, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&mutex->__waiters, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&mutex->__owner, 0, __ATOMIC_RELAXED);
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    if (!mutex)
        return EINVAL;
    if (__atomic_load_n(&mutex->__state, __ATOMIC_ACQUIRE) != 0)
        return EBUSY;
    if (__atomic_load_n(&mutex->__waiters, __ATOMIC_ACQUIRE) != 0)
        return EBUSY;
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex)
{
    if (!mutex)
        return EINVAL;

    int self = gettid();
    for (;;) {
        int expected = 0;
        if (__atomic_compare_exchange_n(&mutex->__state, &expected, 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
            __atomic_store_n(&mutex->__owner, self, __ATOMIC_RELAXED);
            return 0;
        }
        __atomic_fetch_add(&mutex->__waiters, 1, __ATOMIC_ACQ_REL);
        (void)futex_wait(&mutex->__state, 1);
        __atomic_fetch_sub(&mutex->__waiters, 1, __ATOMIC_ACQ_REL);
    }
}

int pthread_mutex_trylock(pthread_mutex_t *mutex)
{
    if (!mutex)
        return EINVAL;

    int expected = 0;
    if (__atomic_compare_exchange_n(&mutex->__state, &expected, 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        __atomic_store_n(&mutex->__owner, gettid(), __ATOMIC_RELAXED);
        return 0;
    }
    return EBUSY;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    if (!mutex)
        return EINVAL;

    if (__atomic_load_n(&mutex->__state, __ATOMIC_ACQUIRE) == 0)
        return EPERM;
    if (__atomic_load_n(&mutex->__owner, __ATOMIC_ACQUIRE) != gettid())
        return EPERM;

    __atomic_store_n(&mutex->__owner, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&mutex->__state, 0, __ATOMIC_RELEASE);
    if (__atomic_load_n(&mutex->__waiters, __ATOMIC_ACQUIRE) > 0)
        (void)futex_wake(&mutex->__state, 1);
    return 0;
}

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
    (void)attr;
    if (!cond)
        return EINVAL;
    __atomic_store_n(&cond->__seq, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&cond->__waiters, 0, __ATOMIC_RELAXED);
    return 0;
}

int pthread_cond_destroy(pthread_cond_t *cond)
{
    if (!cond)
        return EINVAL;
    if (__atomic_load_n(&cond->__waiters, __ATOMIC_ACQUIRE) != 0)
        return EBUSY;
    return 0;
}

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
    if (!cond || !mutex)
        return EINVAL;

    int seq = __atomic_load_n(&cond->__seq, __ATOMIC_RELAXED);
    __atomic_fetch_add(&cond->__waiters, 1, __ATOMIC_ACQ_REL);

    int rc = pthread_mutex_unlock(mutex);
    if (rc != 0) {
        __atomic_fetch_sub(&cond->__waiters, 1, __ATOMIC_ACQ_REL);
        return rc;
    }

    (void)futex_wait(&cond->__seq, seq);
    __atomic_fetch_sub(&cond->__waiters, 1, __ATOMIC_ACQ_REL);

    rc = pthread_mutex_lock(mutex);
    if (rc != 0)
        return rc;
    return 0;
}

int pthread_cond_signal(pthread_cond_t *cond)
{
    if (!cond)
        return EINVAL;

    __atomic_fetch_add(&cond->__seq, 1, __ATOMIC_RELEASE);
    (void)futex_wake(&cond->__seq, 1);
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t *cond)
{
    if (!cond)
        return EINVAL;

    __atomic_fetch_add(&cond->__seq, 1, __ATOMIC_RELEASE);
    (void)futex_wake(&cond->__seq, INT_MAX);
    return 0;
}

int pthread_once(pthread_once_t *once_control, void (*init_routine)(void))
{
    if (!once_control || !init_routine)
        return EINVAL;

    int state = __atomic_load_n(&once_control->__state, __ATOMIC_ACQUIRE);
    if (state == 2)
        return 0;

    int expected = 0;
    if (__atomic_compare_exchange_n(&once_control->__state, &expected, 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        init_routine();
        __atomic_store_n(&once_control->__state, 2, __ATOMIC_RELEASE);
        (void)futex_wake(&once_control->__state, INT_MAX);
        return 0;
    }

    while (__atomic_load_n(&once_control->__state, __ATOMIC_ACQUIRE) != 2)
        (void)futex_wait(&once_control->__state, 1);
    return 0;
}

int pthread_barrier_init(pthread_barrier_t *barrier, const pthread_barrierattr_t *attr, unsigned count)
{
    (void)attr;
    if (!barrier || count == 0)
        return EINVAL;

    barrier->n     = count;
    barrier->count = 0;

    int rc = pthread_mutex_init(&barrier->lock, nullptr);
    if (rc != 0)
        return rc;
    if (sem_init(&barrier->turnstile1, 0) != 0) {
        (void)pthread_mutex_destroy(&barrier->lock);
        return EINVAL;
    }
    if (sem_init(&barrier->turnstile2, 1) != 0) {
        (void)sem_destroy(&barrier->turnstile1);
        (void)pthread_mutex_destroy(&barrier->lock);
        return EINVAL;
    }
    return 0;
}

int pthread_barrier_destroy(pthread_barrier_t *barrier)
{
    if (!barrier)
        return EINVAL;

    int rc = pthread_mutex_destroy(&barrier->lock);
    if (rc != 0)
        return rc;
    if (sem_destroy(&barrier->turnstile1) != 0)
        return EBUSY;
    if (sem_destroy(&barrier->turnstile2) != 0)
        return EBUSY;
    return 0;
}

int pthread_barrier_wait(pthread_barrier_t *barrier)
{
    if (!barrier)
        return EINVAL;

    bool locked = false;
    bool serial = false;

    int rc = pthread_mutex_lock(&barrier->lock);
    if (rc != 0)
        return rc;
    locked = true;

    barrier->count++;
    if (barrier->count == barrier->n) {
        serial = true;
        if (sem_wait(&barrier->turnstile2) != 0) {
            rc = EINVAL;
            goto fail;
        }
        if (sem_post(&barrier->turnstile1) != 0) {
            rc = EINVAL;
            goto fail;
        }
    }

    rc = pthread_mutex_unlock(&barrier->lock);
    if (rc != 0)
        goto fail;
    locked = false;

    if (sem_wait(&barrier->turnstile1) != 0) {
        rc = EINVAL;
        goto fail;
    }
    if (sem_post(&barrier->turnstile1) != 0) {
        rc = EINVAL;
        goto fail;
    }

    rc = pthread_mutex_lock(&barrier->lock);
    if (rc != 0)
        goto fail;
    locked = true;

    barrier->count--;
    if (barrier->count == 0) {
        if (sem_wait(&barrier->turnstile1) != 0) {
            rc = EINVAL;
            goto fail;
        }
        if (sem_post(&barrier->turnstile2) != 0) {
            rc = EINVAL;
            goto fail;
        }
    }

    rc = pthread_mutex_unlock(&barrier->lock);
    if (rc != 0)
        goto fail;

    return serial ? PTHREAD_BARRIER_SERIAL_THREAD : 0;

fail:
    if (locked)
        (void)pthread_mutex_unlock(&barrier->lock);
    return rc;
}

static bool pthread_detached_add_locked(int tid)
{
    int free_slot = -1;
    for (int i = 0; i < PTHREAD_DETACHED_MAX; i++) {
        if (g_detached_table[i].used && g_detached_table[i].tid == tid)
            return false;
        if (!g_detached_table[i].used && free_slot < 0)
            free_slot = i;
    }
    if (free_slot < 0)
        return false;

    g_detached_table[free_slot].used = true;
    g_detached_table[free_slot].tid  = tid;
    return true;
}

static bool pthread_detached_has_locked(int tid)
{
    for (int i = 0; i < PTHREAD_DETACHED_MAX; i++) {
        if (g_detached_table[i].used && g_detached_table[i].tid == tid)
            return true;
    }
    return false;
}

static bool pthread_detached_remove_locked(int tid)
{
    for (int i = 0; i < PTHREAD_DETACHED_MAX; i++) {
        if (g_detached_table[i].used && g_detached_table[i].tid == tid) {
            g_detached_table[i].used = false;
            g_detached_table[i].tid  = 0;
            return true;
        }
    }
    return false;
}

static bool pthread_join_add_locked(int tid)
{
    int free_slot = -1;
    for (int i = 0; i < PTHREAD_JOIN_MAX; i++) {
        if (g_join_table[i].used && g_join_table[i].tid == tid)
            return false;
        if (!g_join_table[i].used && free_slot < 0)
            free_slot = i;
    }
    if (free_slot < 0)
        return false;

    g_join_table[free_slot].used = true;
    g_join_table[free_slot].tid  = tid;
    return true;
}

static bool pthread_join_has_locked(int tid)
{
    for (int i = 0; i < PTHREAD_JOIN_MAX; i++) {
        if (g_join_table[i].used && g_join_table[i].tid == tid)
            return true;
    }
    return false;
}

static void pthread_join_remove_locked(int tid)
{
    for (int i = 0; i < PTHREAD_JOIN_MAX; i++) {
        if (g_join_table[i].used && g_join_table[i].tid == tid) {
            g_join_table[i].used = false;
            g_join_table[i].tid  = 0;
            return;
        }
    }
}

static bool pthread_ret_drop_locked(int tid)
{
    for (int i = 0; i < PTHREAD_RET_MAX; i++) {
        if (g_ret_table[i].used && g_ret_table[i].tid == tid) {
            g_ret_table[i].used  = false;
            g_ret_table[i].tid   = 0;
            g_ret_table[i].value = nullptr;
            return true;
        }
    }
    return false;
}

static void pthread_ret_store(int tid, void *value)
{
    int rc = pthread_mutex_lock(&g_ret_lock);
    if (rc != 0)
        return;

    if (pthread_detached_remove_locked(tid)) {
        (void)pthread_mutex_unlock(&g_ret_lock);
        return;
    }

    int free_slot = -1;
    for (int i = 0; i < PTHREAD_RET_MAX; i++) {
        if (g_ret_table[i].used && g_ret_table[i].tid == tid) {
            g_ret_table[i].value = value;
            (void)pthread_mutex_unlock(&g_ret_lock);
            return;
        }
        if (!g_ret_table[i].used && free_slot < 0)
            free_slot = i;
    }

    if (free_slot >= 0) {
        g_ret_table[free_slot].used  = true;
        g_ret_table[free_slot].tid   = tid;
        g_ret_table[free_slot].value = value;
    }

    (void)pthread_mutex_unlock(&g_ret_lock);
}

static void *pthread_ret_take(int tid)
{
    void *value = nullptr;

    int rc = pthread_mutex_lock(&g_ret_lock);
    if (rc != 0)
        return nullptr;

    for (int i = 0; i < PTHREAD_RET_MAX; i++) {
        if (g_ret_table[i].used && g_ret_table[i].tid == tid) {
            value                = g_ret_table[i].value;
            g_ret_table[i].used  = false;
            g_ret_table[i].tid   = 0;
            g_ret_table[i].value = nullptr;
            break;
        }
    }

    (void)pthread_mutex_unlock(&g_ret_lock);
    return value;
}

static void pthread_trampoline(void *arg)
{
    __tls_init_thread();

    g_start_ctx = (struct pthread_start *)arg;

    void *ret = nullptr;
    if (g_start_ctx && g_start_ctx->start)
        ret = g_start_ctx->start(g_start_ctx->arg);

    pthread_exit(ret);
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg)
{
    (void)attr;
    if (!thread || !start_routine)
        return EINVAL;

    struct pthread_start *start = malloc(sizeof(*start));
    if (!start)
        return ENOMEM;

    start->start = start_routine;
    start->arg   = arg;

    int tid = thread_create(pthread_trampoline, start);
    if (tid < 0) {
        free(start);
        return EAGAIN;
    }

    *thread = tid;
    return 0;
}

[[noreturn]] void pthread_exit(void *retval)
{
    auto start = g_start_ctx;
    g_start_ctx = nullptr;
    if (start)
        free(start);

    __tls_destroy_thread();
    pthread_ret_store(gettid(), retval);
    thread_exit(0);
    __builtin_unreachable();
}

int pthread_join(pthread_t thread, void **retval)
{
    if (thread <= 0)
        return ESRCH;
    if (thread == pthread_self())
        return EDEADLK;

    int rc = pthread_mutex_lock(&g_ret_lock);
    if (rc != 0)
        return rc;

    if (pthread_detached_has_locked(thread)) {
        (void)pthread_mutex_unlock(&g_ret_lock);
        return EINVAL;
    }

    if (!pthread_join_add_locked(thread)) {
        (void)pthread_mutex_unlock(&g_ret_lock);
        return EINVAL;
    }

    rc = pthread_mutex_unlock(&g_ret_lock);
    if (rc != 0)
        return rc;

    int status = 0;
    int join_rc = thread_join(thread, &status);

    rc = pthread_mutex_lock(&g_ret_lock);
    if (rc != 0)
        return rc;

    pthread_join_remove_locked(thread);
    bool detached = pthread_detached_has_locked(thread);

    rc = pthread_mutex_unlock(&g_ret_lock);
    if (rc != 0)
        return rc;

    if (join_rc != 0)
        return detached ? EINVAL : ESRCH;

    if (retval)
        *retval = pthread_ret_take(thread);
    return 0;
}

int pthread_detach(pthread_t thread)
{
    if (thread <= 0)
        return ESRCH;

    int rc = pthread_mutex_lock(&g_ret_lock);
    if (rc != 0)
        return rc;

    if (pthread_join_has_locked(thread) || pthread_detached_has_locked(thread)) {
        (void)pthread_mutex_unlock(&g_ret_lock);
        return EINVAL;
    }

    rc = pthread_mutex_unlock(&g_ret_lock);
    if (rc != 0)
        return rc;

    if (thread_detach(thread) != 0) {
        rc = pthread_mutex_lock(&g_ret_lock);
        if (rc != 0)
            return rc;

        bool detached = pthread_detached_has_locked(thread);

        rc = pthread_mutex_unlock(&g_ret_lock);
        if (rc != 0)
            return rc;

        return detached ? EINVAL : ESRCH;
    }

    rc = pthread_mutex_lock(&g_ret_lock);
    if (rc != 0)
        return rc;

    bool had_ret = pthread_ret_drop_locked(thread);
    if (!had_ret && !pthread_detached_add_locked(thread)) {
        (void)pthread_mutex_unlock(&g_ret_lock);
        return EAGAIN;
    }

    rc = pthread_mutex_unlock(&g_ret_lock);
    if (rc != 0)
        return rc;

    return 0;
}

pthread_t pthread_self(void)
{
    return gettid();
}
