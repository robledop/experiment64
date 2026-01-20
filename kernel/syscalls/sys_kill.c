#include <task/signal.h>

int sys_kill(int pid, int sig)
{
    return signal_send_pid(pid, sig);
}
