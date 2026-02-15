#pragma once
#include <pthread_types.h>
#include <pthread.h>


int sem_init(sem_t *s, int value);
int sem_wait(sem_t *s);
int sem_post(sem_t *s);
int sem_destroy(sem_t *s);
