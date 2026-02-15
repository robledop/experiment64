#pragma once

#include <assert.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>

#define Pthread_create(thread, attr, start_routine, arg)                                                               \
    if (pthread_create(thread, attr, start_routine, arg) != 0)                                                         \
        panic("Failed to create thread");
#define Pthread_join(thread, value_ptr)                                                                                \
    if (pthread_join(thread, value_ptr) != 0)                                                                          \
        panic("Failed to join thread");

#define Pthread_mutex_init(m, v)                                                                                       \
    if (pthread_mutex_init(m, v) != 0)                                                                                 \
        panic("Failed to init mutex");
#define Pthread_mutex_lock(m)                                                                                          \
    if (pthread_mutex_lock(m) != 0)                                                                                    \
        panic("Failed to lock mutex");
#define Pthread_mutex_unlock(m)                                                                                        \
    if (pthread_mutex_unlock(m) != 0)                                                                                  \
        panic("Failed to unlock mutex");

#define Pthread_cond_init(cond, v)                                                                                     \
    if (pthread_cond_init(cond, v) != 0)                                                                               \
        panic("Failed to init condition variable");
#define Pthread_cond_signal(cond)                                                                                      \
    if (pthread_cond_signal(cond) != 0)                                                                                \
        panic("Failed to signal condition variable");
#define Pthread_cond_wait(cond, mutex)                                                                                 \
    if (pthread_cond_wait(cond, mutex) != 0)                                                                           \
        panic("Failed to wait on condition variable");

#define Mutex_init(m)                                                                                                  \
    if (pthread_mutex_init(m, nullptr) != 0)                                                                              \
        panic("Failed to init mutex");
#define Mutex_lock(m)                                                                                                  \
    if (pthread_mutex_lock(m) != 0)                                                                                    \
        panic("Failed to lock mutex");
#define Mutex_unlock(m)                                                                                                \
    if (pthread_mutex_unlock(m) != 0)                                                                                  \
        panic("Failed unlock mutex");

#define Cond_init(cond)                                                                                                \
    if (pthread_cond_init(cond, nullptr) != 0)                                                                            \
        panic("Failed to init condition variable");
#define Cond_signal(cond)                                                                                              \
    if (pthread_cond_signal(cond) != 0)                                                                                \
        panic("Failed to signal condition variable");
#define Cond_wait(cond, mutex)                                                                                         \
    if (pthread_cond_wait(cond, mutex) != 0)                                                                           \
        panic("Failed to wait on condition variable");

#define Sem_init(sem, value)                                                                                           \
    if (sem_init(sem, value) != 0)                                                                                     \
        panic("Failed to init semaphore");
#define Sem_wait(sem)                                                                                                  \
    if (sem_wait(sem) != 0)                                                                                            \
        panic("Failed to wait on semaphore");
#define Sem_post(sem)                                                                                                  \
    if (sem_post(sem) != 0)                                                                                            \
        panic("Failed to post semaphore");
