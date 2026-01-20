#include <signal.h>
#include <unistd.h>

int main(void)
{
    const int pid = getpid();
    if (kill(pid, SIGTERM) < 0)
        return 78;
    return 77;
}
