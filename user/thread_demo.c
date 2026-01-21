#include <stdio.h>
#include <unistd.h>

static constexpr char k_thread_msg[] = "thread_demo: worker tick ";

// ReSharper disable once CppParameterMayBeConstPtrOrRef
static void thread_entry(void* arg)
{
    auto msg = (const char*)arg;
    for (int i = 0; i < 5; i++)
    {
        printf("%s%d\n", msg, i);
        sleep(100);
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
        sleep(120);
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
