#pragma once

typedef int pthread_t;

typedef struct {
    int __state;
    int __waiters;
} pthread_mutex_t;

typedef struct {
    int __seq;
    int __waiters;
} pthread_cond_t;

typedef struct {
    int __state;
} pthread_once_t;

typedef struct {
    int __dummy;
} pthread_attr_t;

typedef struct semaphore {
    int value;
    pthread_cond_t cond;
    pthread_mutex_t lock;
} sem_t;

typedef struct {
    unsigned n;
    unsigned count;
    pthread_mutex_t lock;
    sem_t turnstile1; // initially 0 (closed)
    sem_t turnstile2; // initially 1 (open)
} barrier_t;


#define PTHREAD_MUTEX_INITIALIZER {0, 0}
#define PTHREAD_COND_INITIALIZER {0, 0}
#define PTHREAD_ONCE_INIT {0}
