#pragma once
#include <pthread.h>

typedef struct semaphore {
    int value;
    pthread_cond_t cond;
    pthread_mutex_t lock;
} sem_t;

int sem_init(sem_t *s, int value);
int sem_wait(sem_t *s);
int sem_post(sem_t *s);
