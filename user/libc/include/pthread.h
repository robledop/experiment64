#pragma once

#include <stddef.h>
#include <stdint.h>

typedef int pthread_t;

typedef struct
{
    int __state;
} pthread_mutex_t;

typedef struct
{
    int __seq;
} pthread_cond_t;

typedef struct
{
    int __state;
} pthread_once_t;

typedef struct
{
    int __dummy;
} pthread_attr_t;

#define PTHREAD_MUTEX_INITIALIZER {0}
#define PTHREAD_COND_INITIALIZER {0}
#define PTHREAD_ONCE_INIT {0}

int pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                   void* (*start_routine)(void*), void* arg);
[[noreturn]] void pthread_exit(void* retval);
int pthread_join(pthread_t thread, void** retval);
int pthread_detach(pthread_t thread);
pthread_t pthread_self(void);
static inline int pthread_equal(pthread_t a, pthread_t b) { return a == b; }

int pthread_mutex_init(pthread_mutex_t* mutex, const void* attr);
int pthread_mutex_destroy(pthread_mutex_t* mutex);
int pthread_mutex_lock(pthread_mutex_t* mutex);
int pthread_mutex_trylock(pthread_mutex_t* mutex);
int pthread_mutex_unlock(pthread_mutex_t* mutex);

int pthread_cond_init(pthread_cond_t* cond, const void* attr);
int pthread_cond_destroy(pthread_cond_t* cond);
int pthread_cond_wait(pthread_cond_t* cond, pthread_mutex_t* mutex);
int pthread_cond_signal(pthread_cond_t* cond);
int pthread_cond_broadcast(pthread_cond_t* cond);

int pthread_once(pthread_once_t* once_control, void (*init_routine)(void));
