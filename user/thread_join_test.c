#include <unistd.h>

static void join_worker(void* arg)
{
    (void)arg;
    thread_exit(42);
}

int main(void)
{
    int tid = thread_create(join_worker, nullptr);
    if (tid < 0)
        return 1;
    int status = 0;
    if (thread_join(tid, &status) != 0)
        return 2;
    if (status != 42)
        return 3;
    return 0;
}
