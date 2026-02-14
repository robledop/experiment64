#include <tests/test.h>
#include <sys/signal.h>
#include <sys/syscall.h>
#include <drivers/keyboard.h>
#include <drivers/terminal.h>

#define SC_CTRL_PRESS 0x1D
#define SC_CTRL_RELEASE 0x9D
#define SC_C 0x2E

static bool signal_wait_pid(const int target_pid, int* status_out)
{
    for (;;)
    {
        int status = 0;
        const int pid = sys_wait(&status);
        if (pid < 0)
            return false;
        if (pid == target_pid)
        {
            if (status_out)
                *status_out = status;
            return true;
        }
    }
}

TEST (test_signal_sigaction_invalid)
{
    sigaction_t act = {};
    act.sa_handler = (sighandler_t)0x1234;

    TEST_ASSERT(sys_sigaction(SIGUSR1, &act, nullptr) == -1);
    TEST_ASSERT(sys_sigaction(SIGKILL, &act, nullptr) == -1);
    TEST_ASSERT(sys_sigaction(SIGSTOP, &act, nullptr) == -1);
    TEST_ASSERT(sys_sigaction(0, &act, nullptr) == -1);
    TEST_ASSERT(sys_sigaction(SIG_MAX + 1, &act, nullptr) == -1);

    act.sa_handler = SIG_IGN;
    TEST_ASSERT(sys_sigaction(SIGUSR1, &act, nullptr) == 0);
    return true;
}

TEST (test_signal_user_handler)
{
    const int pid = sys_spawn("/tests/sigtest_handler");
    TEST_ASSERT(pid > 1);

    int status = 0;
    TEST_ASSERT(signal_wait_pid(pid, &status));
    TEST_ASSERT(status == 0);
    return true;
}

TEST (test_signal_sigchld)
{
    const int pid = sys_spawn("/tests/sigtest_sigchld");
    TEST_ASSERT(pid > 1);

    int status = 0;
    TEST_ASSERT(signal_wait_pid(pid, &status));
    if (status != 10)
        printk("test_signal_sigchld: pid=%d status=%d\n", pid, status);
    TEST_ASSERT(status == 10);
    return true;
}

TEST (test_signal_nocldwait)
{
    const int pid = sys_spawn("/tests/sigtest_nocldwait");
    TEST_ASSERT(pid > 1);

    int status = 0;
    TEST_ASSERT(signal_wait_pid(pid, &status));
    if (status != 12)
        printk("test_signal_nocldwait: pid=%d status=%d\n", pid, status);
    TEST_ASSERT(status == 12);
    return true;
}

TEST (test_signal_mask)
{
    const int pid = sys_spawn("/tests/sigtest_mask");
    TEST_ASSERT(pid > 1);

    int status = 0;
    TEST_ASSERT(signal_wait_pid(pid, &status));
    if (status != 11)
        printk("test_signal_mask: pid=%d status=%d\n", pid, status);
    TEST_ASSERT(status == 11);
    return true;
}

TEST (test_signal_default_terminate)
{
    const int pid = sys_spawn("/tests/sigtest_term");
    TEST_ASSERT(pid > 1);

    int status = 0;
    TEST_ASSERT(signal_wait_pid(pid, &status));
    if (status != 128 + SIGTERM)
        printk("test_signal_default_terminate: pid=%d status=%d\n", pid, status);
    TEST_ASSERT(status == 128 + SIGTERM);
    return true;
}

TEST (test_signal_ctrl_c)
{
    const int pid = sys_spawn("/tests/sigtest_int");
    TEST_ASSERT(pid > 1);

    keyboard_set_foreground_pid(pid);
    keyboard_inject_scancode(SC_CTRL_PRESS);
    keyboard_inject_scancode(SC_C);
    keyboard_inject_scancode(SC_CTRL_RELEASE);
    keyboard_set_foreground_pid(0);

    int status = 0;
    TEST_ASSERT(signal_wait_pid(pid, &status));
    if (status != 128 + SIGINT)
        printk("test_signal_ctrl_c: pid=%d status=%d\n", pid, status);
    TEST_ASSERT(status == 128 + SIGINT);
    return true;
}
