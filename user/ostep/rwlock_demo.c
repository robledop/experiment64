#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct _rwlock_t {
    sem_t writelock;
    sem_t lock;
    int readers;
} rwlock_t;

static bool parse_nonnegative_int(const char *text, int *out)
{
    if (!text || !out || *text == '\0')
        return false;
    errno = 0;
    char *end = nullptr;
    const long value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < 0 || value > INT_MAX)
        return false;
    *out = (int)value;
    return true;
}

void rwlock_init(rwlock_t *lock)
{
    lock->readers = 0;
    sem_init(&lock->lock, 1);
    sem_init(&lock->writelock, 1);
}

void rwlock_acquire_readlock(rwlock_t *lock)
{
    sem_wait(&lock->lock);
    lock->readers++;
    if (lock->readers == 1)
        sem_wait(&lock->writelock);
    sem_post(&lock->lock);
}

void rwlock_release_readlock(rwlock_t *lock)
{
    sem_wait(&lock->lock);
    lock->readers--;
    if (lock->readers == 0)
        sem_post(&lock->writelock);
    sem_post(&lock->lock);
}

void rwlock_acquire_writelock(rwlock_t *lock)
{
    sem_wait(&lock->writelock);
}

void rwlock_release_writelock(rwlock_t *lock)
{
    sem_post(&lock->writelock);
}

int read_loops;
int write_loops;
int counter = 0;

rwlock_t mutex;

void *reader(void *arg)
{
    int i;
    int local = 0;
    for (i = 0; i < read_loops; i++) {
        rwlock_acquire_readlock(&mutex);
        local = counter;
        rwlock_release_readlock(&mutex);
        printf("read %d\n", local);
    }
    printf("read done: %d\n", local);
    return NULL;
}

void *writer(void *arg)
{
    int i;
    for (i = 0; i < write_loops; i++) {
        rwlock_acquire_writelock(&mutex);
        counter++;
        rwlock_release_writelock(&mutex);
    }
    printf("write done\n");
    return NULL;
}

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "usage: rwlock readloops writeloops\n");
        exit(1);
    }
    if (!parse_nonnegative_int(argv[1], &read_loops) || !parse_nonnegative_int(argv[2], &write_loops)) {
        fprintf(stderr, "invalid loop count\n");
        exit(1);
    }

    rwlock_init(&mutex);
    pthread_t c1, c2;
    pthread_create(&c1, nullptr, reader, nullptr);
    pthread_create(&c2, nullptr, writer, nullptr);
    pthread_join(c1, nullptr);
    pthread_join(c2, nullptr);
    printf("all done\n");
    return 0;
}
