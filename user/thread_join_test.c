#include <unistd.h>

static void join_worker(void* arg)
{
    (void)arg;
    thread_exit(0);
}

int main(void)
{
    int tid = thread_create(join_worker, nullptr);
    if (tid < 0)
        return 1;
    if (thread_join(tid) != 0)
        return 2;
    return 0;
}
