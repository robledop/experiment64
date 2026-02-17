# Minimal Pthreads (Userland)

The libc provides a small pthread subset built on user threads and futexes.

Implemented:

- `pthread_create`, `pthread_exit`, `pthread_join`, `pthread_detach`, `pthread_self`
- `pthread_mutex_*` (normal mutexes only)
- `pthread_cond_*`
- `pthread_once`
- `pthread_barrier_*`
- `sem_init`, `sem_wait`, `sem_post`, `sem_destroy`

Notes:

- Pthread functions return `0` on success and an error number on failure.
- `pthread_mutex_destroy()` returns `EBUSY` if the mutex is locked or has waiters.
- `pthread_cond_destroy()` returns `EBUSY` if threads are currently waiting on
  the condition variable.
- `pthread_barrier_wait()` returns `PTHREAD_BARRIER_SERIAL_THREAD` for one
  participant and `0` for the others.
- POSIX semaphore functions return `0` on success, and `-1` with `errno` set on
  failure.

Each new thread created via `pthread_create` gets its own TLS block
initialized before the user start routine runs, and `pthread_exit()` tears it
down before terminating the thread. See `docs/tls.md` for details.

Example:

```c
#include <pthread.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static int ready = 0;

static void* worker(void* arg)
{
    (void)arg;
    pthread_mutex_lock(&lock);
    ready = 1;
    pthread_cond_signal(&cv);
    pthread_mutex_unlock(&lock);
    return nullptr;
}
```
