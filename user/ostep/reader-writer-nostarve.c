#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include "common_threads.h"

//
// Your code goes in the structure and functions below
//

typedef struct __rwlock_t {
} rwlock_t;


void rwlock_init(rwlock_t *rw)
{
}

void rwlock_acquire_readlock(rwlock_t *rw)
{
}

void rwlock_release_readlock(rwlock_t *rw)
{
}

void rwlock_acquire_writelock(rwlock_t *rw)
{
}

void rwlock_release_writelock(rwlock_t *rw)
{
}

//
// Don't change the code below (just use it!)
//

int loops;
int value = 0;

rwlock_t lock;

static int parse_int_arg(const char *s)
{
    char *end = nullptr;
    long value = strtol(s, &end, 10);
    assert(end != s);
    assert(*end == '\0');
    assert(value >= INT_MIN && value <= INT_MAX);
    return (int)value;
}

void *reader(void *arg)
{
    int i;
    for (i = 0; i < loops; i++) {
        rwlock_acquire_readlock(&lock);
        printf("read %d\n", value);
        rwlock_release_readlock(&lock);
    }
    return nullptr;
}

void *writer(void *arg)
{
    int i;
    for (i = 0; i < loops; i++) {
        rwlock_acquire_writelock(&lock);
        value++;
        printf("write %d\n", value);
        rwlock_release_writelock(&lock);
    }
    return nullptr;
}

int main(int argc, char *argv[])
{
    assert(argc == 4);
    int num_readers = parse_int_arg(argv[1]);
    int num_writers = parse_int_arg(argv[2]);
    loops           = parse_int_arg(argv[3]);

    pthread_t pr[num_readers], pw[num_writers];

    rwlock_init(&lock);

    printf("begin\n");

    int i;
    for (i = 0; i < num_readers; i++)
        Pthread_create(&pr[i], nullptr, reader, nullptr);
    for (i = 0; i < num_writers; i++)
        Pthread_create(&pw[i], nullptr, writer, nullptr);

    for (i = 0; i < num_readers; i++)
        Pthread_join(pr[i], nullptr);
    for (i = 0; i < num_writers; i++)
        Pthread_join(pw[i], nullptr);

    printf("end: value %d\n", value);

    return 0;
}
