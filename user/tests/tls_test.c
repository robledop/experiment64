#include <pthread.h>

static thread_local int tls_var = 42;
static thread_local int tls_zero;

static void *worker(void *arg)
{
    (void)arg;

    if (tls_var != 42)
        return (void *)1;
    if (tls_zero != 0)
        return (void *)2;

    tls_var = 100;
    tls_zero = 200;

    if (tls_var != 100)
        return (void *)3;
    if (tls_zero != 200)
        return (void *)4;

    return (void *)0;
}

int main(void)
{
    if (tls_var != 42)
        return 1;
    if (tls_zero != 0)
        return 2;

    tls_var = 99;

    pthread_t t;
    if (pthread_create(&t, nullptr, worker, nullptr) != 0)
        return 3;

    void *ret = nullptr;
    if (pthread_join(t, &ret) != 0)
        return 4;
    if (ret != (void *)0)
        return 5;

    if (tls_var != 99)
        return 6;
    if (tls_zero != 0)
        return 7;

    return 0;
}
