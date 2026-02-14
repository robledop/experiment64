#include <semaphore.h>
#include <stdlib.h>
#include <util.h>

static void cond_init(pthread_cond_t *cond)
{
    if (pthread_cond_init(cond, nullptr) != 0) {
        panic("Failed to initialize condition variable");
    }
}

static void mutex_init(pthread_mutex_t *lock)
{
    if (pthread_mutex_init(lock, nullptr) != 0) {
        panic("Failed to initialize mutex");
    }
}

static void mutex_lock(pthread_mutex_t *lock)
{
    if (pthread_mutex_lock(lock) != 0) {
        panic("Failed to lock mutex");
    }
}

static void mutex_unlock(pthread_mutex_t *lock)
{
    if (pthread_mutex_unlock(lock) != 0) {
        panic("Failed to unlock mutex");
    }
}

static void cond_wait(pthread_cond_t *cond, pthread_mutex_t *lock)
{
    if (pthread_cond_wait(cond, lock) != 0) {
        panic("Failed to wait on condition variable");
    }
}

static void cond_signal(pthread_cond_t *cond)
{
    if (pthread_cond_signal(cond) != 0) {
        panic("Failed to signal conditional variable");
    }
}

void sem_init(sem_t *s, const int value)
{
    if (value < 0) {
        panic("Value must be greater than 0");
    }

    s->value = value;
    cond_init(&s->cond);
    mutex_init(&s->lock);
}

void sem_wait(sem_t *s)
{
    mutex_lock(&s->lock);
    while (s->value <= 0) {
        cond_wait(&s->cond, &s->lock);
    }
    s->value--;
    mutex_unlock(&s->lock);
}

void sem_post(sem_t *s)
{
    mutex_lock(&s->lock);
    s->value = clamp_signed_to_int(s->value + 1);
    cond_signal(&s->cond);
    mutex_unlock(&s->lock);
}
