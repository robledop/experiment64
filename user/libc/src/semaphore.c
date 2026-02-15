#include <semaphore.h>
#include <stdlib.h>
#include <util.h>

int sem_init(sem_t *s, const int value)
{
    if (value < 0) {
        panic("Value must be greater than 0");
    }

    s->value = value;
    if (pthread_cond_init(&s->cond, nullptr) != 0) {
        return -1;
    }
    if (pthread_mutex_init(&s->lock, nullptr) != 0) {
        return -1;
    }

    return 0;
}

int sem_wait(sem_t *s)
{
    if (pthread_mutex_lock(&s->lock) != 0) {
        return -1;
    }
    while (s->value <= 0) {
        if (pthread_cond_wait(&s->cond, &s->lock) != 0) {
            return -1;
        }
    }
    s->value--;
    if (pthread_mutex_unlock(&s->lock) != 0) {
        return -1;
    }

    return 0;
}

int sem_post(sem_t *s)
{
    if (pthread_mutex_lock(&s->lock) != 0) {
        return -1;
    }
    s->value = clamp_signed_to_int(s->value + 1);
    if (pthread_cond_signal(&s->cond) != 0) {
        return -1;
    }
    if (pthread_mutex_unlock(&s->lock) != 0) {
        return -1;
    }

    return 0;
}
