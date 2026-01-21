#include <stdio.h>
#include <unistd.h>

static constexpr char k_thread_msg[] = "thread_demo: worker tick ";
static volatile int g_futex_word = 0;

// ReSharper disable once CppParameterMayBeConstPtrOrRef
static void thread_entry(void* arg)
{
    auto msg = (const char*)arg;
    for (int i = 0; i < 5; i++)
    {
        const int wait_val = i * 2;
        while (__atomic_load_n(&g_futex_word, __ATOMIC_ACQUIRE) != (i * 2 + 1))
        {
            futex_wait(&g_futex_word, wait_val);
        }

        printf("%s%d\n", msg, i);
        __atomic_store_n(&g_futex_word, i * 2 + 2, __ATOMIC_RELEASE);
        futex_wake(&g_futex_word, 1);
    }

    thread_exit(0);
}

int main(void)
{
    printf("thread_demo: starting\n");
    int tid = thread_create(thread_entry, (void*)k_thread_msg);
    if (tid < 0)
    {
        printf("thread_demo: thread_create failed\n");
        return 1;
    }

    printf("thread_demo: created tid=%d\n", tid);
    for (int i = 0; i < 5; i++)
    {
        printf("thread_demo: main tick %d\n", i);
        __atomic_store_n(&g_futex_word, i * 2 + 1, __ATOMIC_RELEASE);
        futex_wake(&g_futex_word, 1);

        const int wait_val = i * 2 + 1;
        while (__atomic_load_n(&g_futex_word, __ATOMIC_ACQUIRE) != (i * 2 + 2))
        {
            futex_wait(&g_futex_word, wait_val);
        }
    }

    int status = 0;
    if (thread_join(tid, &status) != 0)
    {
        printf("thread_demo: thread_join failed\n");
        return 1;
    }

    printf("thread_demo: joined tid=%d status=%d\n", tid, status);
    printf("thread_demo: exiting\n");
    return 0;
}
