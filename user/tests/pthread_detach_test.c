#include <pthread.h>
#include <unistd.h>

static void* slow_worker(void* arg)
{
    (void)arg;
    for (int i = 0; i < 100; i++)
        yield();
    return (void*)1;
}

static void* fast_worker(void* arg)
{
    (void)arg;
    return (void*)2;
}

int main(void)
{
    pthread_t slow;
    if (pthread_create(&slow, nullptr, slow_worker, nullptr) != 0)
        return 1;
    if (pthread_detach(slow) != 0)
        return 2;

    pthread_t fast;
    if (pthread_create(&fast, nullptr, fast_worker, nullptr) != 0)
        return 3;

    sleep(10);
    if (pthread_detach(fast) != 0)
        return 4;

    sleep(10);
    return 0;
}
