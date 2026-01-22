# Minimal Pthreads (Userland)

The libc provides a small pthread subset built on user threads and futexes.

Implemented:

- `pthread_create`, `pthread_exit`, `pthread_join`, `pthread_detach`, `pthread_self`
- `pthread_mutex_*` (normal mutexes only)
- `pthread_cond_*`
- `pthread_once`

Behavior and limits:

- Mutexes and condition variables are process-local and use futex wait/wake.
- Condition variables may wake spuriously; callers must re-check predicates.
- No timed waits or cancellation.
- The thread return value is stored in libc, not the kernel. Joining clears it.
- Detached threads are non-joinable and discard return values.

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
