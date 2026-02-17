#include <errno.h>
#include <limits.h>
#include <semaphore.h>
#include <status.h>

static int sem_error(const int error)
{
    errno = error;
    return -1;
}

int sem_init(sem_t *s, const int value)
{
    if (!s)
        return sem_error(EINVAL);
    if (value < 0)
        return sem_error(EINVAL);

    int rc = pthread_cond_init(&s->cond, nullptr);
    if (rc != 0)
        return sem_error(rc);

    rc = pthread_mutex_init(&s->lock, nullptr);
    if (rc != 0) {
        (void)pthread_cond_destroy(&s->cond);
        return sem_error(rc);
    }

    s->value = value;

    return ALL_OK;
}

int sem_wait(sem_t *s)
{
    if (!s)
        return sem_error(EINVAL);

    int rc = pthread_mutex_lock(&s->lock);
    if (rc != 0)
        return sem_error(rc);

    while (s->value <= 0) {
        rc = pthread_cond_wait(&s->cond, &s->lock);
        if (rc != 0) {
            const int unlock_rc = pthread_mutex_unlock(&s->lock);
            if (unlock_rc != 0)
                rc = unlock_rc;
            return sem_error(rc);
        }
    }
    s->value--;
    rc = pthread_mutex_unlock(&s->lock);
    if (rc != 0)
        return sem_error(rc);

    return ALL_OK;
}

int sem_post(sem_t *s)
{
    if (!s)
        return sem_error(EINVAL);

    int rc = pthread_mutex_lock(&s->lock);
    if (rc != 0)
        return sem_error(rc);

    if (s->value == INT_MAX) {
        rc = EAGAIN;
        const int unlock_rc = pthread_mutex_unlock(&s->lock);
        if (unlock_rc != 0)
            rc = unlock_rc;
        return sem_error(rc);
    }

    s->value++;

    rc = pthread_cond_signal(&s->cond);
    if (rc != 0) {
        const int unlock_rc = pthread_mutex_unlock(&s->lock);
        if (unlock_rc != 0)
            rc = unlock_rc;
        return sem_error(rc);
    }

    rc = pthread_mutex_unlock(&s->lock);
    if (rc != 0)
        return sem_error(rc);

    return ALL_OK;
}
int sem_destroy(sem_t *s)
{
    if (!s)
        return sem_error(EINVAL);

    int rc = pthread_mutex_destroy(&s->lock);
    if (rc != 0)
        return sem_error(rc);

    rc = pthread_cond_destroy(&s->cond);
    if (rc != 0)
        return sem_error(rc);

    return ALL_OK;
}
