#include <stdio.h>
#include <pthread.h>

static constexpr char pthread_msg[] = "thread_demo: worker tick ";
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
#define ITERATIONS 10

// ReSharper disable once CppParameterMayBeConstPtrOrRef
static void* thread_entry(void* arg)
{
    auto msg = (const char*)arg;
    for (int i = 0; i < ITERATIONS; i++)
    {
        pthread_mutex_lock(&g_mutex);
        printf("%s%d\n", msg, i);
        pthread_mutex_unlock(&g_mutex);
    }

    thread_exit(0);
}

long fib_recursive(long n)
{
    if (n <= 1) return n;
    return fib_recursive(n - 1) + fib_recursive(n - 2);
}

static void* fibonacci(void* arg)
{
    (void)arg;
    long b = fib_recursive(50);
    pthread_mutex_lock(&g_mutex);
    printf("tid: %d, fibonacci(50): %ld\n", pthread_self(), b);
    pthread_mutex_unlock(&g_mutex);
    thread_exit(0);
}

int main(void)
{
    pthread_mutex_init(&g_mutex, nullptr);

    printf("thread_demo: starting\n");
    pthread_t thread;
    if (pthread_create(&thread, nullptr, thread_entry, (void*)pthread_msg) != 0)
    {
        printf("thread_demo: thread_create failed\n");
        return 1;
    }
    pthread_detach(thread);

    for (int i = 0; i < ITERATIONS; i++)
    {
        pthread_mutex_lock(&g_mutex);
        printf("thread_demo: main tick %d\n", i);
        pthread_mutex_unlock(&g_mutex);
    }

    printf("Calculating fibonacci in 20 threads. Press CTRL+P to see the list of threads\n");
    for (int i = 0; i < 20; i++)
    {
        pthread_t t;
        pthread_create(&t, nullptr, fibonacci, nullptr);
        pthread_detach(t);
    }

    printf("thread_demo: exiting\n");
    return 0;
}
