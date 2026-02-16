#include <tests/test.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <task/process.h>

TEST(test_sys_thread_join_basic)
{
    int pid = sys_spawn("/tests/thread_join_test");
    TEST_ASSERT(pid > 1);
    int status = -1;
    int waited = sys_wait(&status);
    TEST_ASSERT(waited == pid);
    TEST_ASSERT(status == 0);
    return true;
}

TEST(test_sys_thread_stack_alignment)
{
    int pid = sys_spawn("/tests/thread_stack_align_test");
    TEST_ASSERT(pid > 1);
    int status = -1;
    int waited = sys_wait(&status);
    TEST_ASSERT(waited == pid);
    TEST_ASSERT(status == 0);
    return true;
}

TEST(test_sys_waitpid_basic)
{
    int pid = sys_spawn("/tests/waitpid_test");
    TEST_ASSERT(pid > 1);
    int status = -1;
    int waited = sys_wait(&status);
    TEST_ASSERT(waited == pid);
    TEST_ASSERT(status == 14);
    return true;
}

TEST(test_sys_gettid_basic)
{
    int pid = sys_spawn("/tests/gettid_test");
    TEST_ASSERT(pid > 1);
    int status = -1;
    int waited = sys_wait(&status);
    TEST_ASSERT(waited == pid);
    TEST_ASSERT(status == 0);
    return true;
}

TEST(test_sys_pthread_basic)
{
    int pid = sys_spawn("/tests/pthread_test");
    TEST_ASSERT(pid > 1);
    int status = -1;
    int waited = sys_wait(&status);
    TEST_ASSERT(waited == pid);
    TEST_ASSERT(status == 0);
    return true;
}

TEST(test_sys_pthread_detach_basic)
{
    int pid = sys_spawn("/tests/pthread_detach_test");
    TEST_ASSERT(pid > 1);
    int status = -1;
    int waited = sys_wait(&status);
    TEST_ASSERT(waited == pid);
    TEST_ASSERT(status == 0);
    return true;
}

TEST(test_sys_semaphore_basic)
{
    int pid = sys_spawn("/tests/sem_test");
    TEST_ASSERT(pid > 1);
    int status = -1;
    int waited = sys_wait(&status);
    TEST_ASSERT(waited == pid);
    TEST_ASSERT(status == 0);
    return true;
}

TEST(test_sys_tls_basic)
{
    int pid = sys_spawn("/tests/tls_test");
    TEST_ASSERT(pid > 1);
    int status = -1;
    int waited = sys_wait(&status);
    TEST_ASSERT(waited == pid);
    TEST_ASSERT(status == 0);
    return true;
}

TEST(test_sys_gettimeofday_basic)
{
    struct timeval tv = {0};
    struct timezone tz = {0};
    const int rc = sys_gettimeofday(&tv, &tz);
    TEST_ASSERT(rc == 0);
    // Should return some non-negative time; accept zero if the clock not initialized yet.
    TEST_ASSERT(tv.tv_usec >= 0);
    TEST_ASSERT(tv.tv_usec < 1000000);
    TEST_ASSERT(tv.tv_sec >= 0);
    TEST_ASSERT(tz.tz_minuteswest == 0);
    TEST_ASSERT(tz.tz_dsttime == 0);
    return true;
}

static volatile uint32_t g_futex_word = 0;
static volatile bool g_futex_done = false;

static void futex_waker_entry(void)
{
    while (!g_futex_done)
    {
        if (__atomic_load_n(&g_futex_word, __ATOMIC_RELAXED) == 1)
            sys_futex_wake((uint32_t*)&g_futex_word, 1);
        yield();
    }
}

TEST(test_sys_futex_wait_wake)
{
    g_futex_word = 0;
    g_futex_done = false;

    thread_t* t = thread_create(current_process, futex_waker_entry, false);
    if (!t)
        return false;
    thread_make_ready(t);

    __atomic_store_n(&g_futex_word, 1, __ATOMIC_RELAXED);
    int rc = sys_futex_wait((uint32_t*)&g_futex_word, 1);
    g_futex_done = true;

    TEST_ASSERT(rc == 0);
    return true;
}

TEST(test_sys_futex_wait_mismatch)
{
    g_futex_word = 0;
    int rc = sys_futex_wait((uint32_t*)&g_futex_word, 1);
    TEST_ASSERT(rc == -1);
    return true;
}

TEST(test_sys_ioctl_tiocgwinsz)
{
    struct winsize ws = {0};
    int fd = sys_open("/dev/console", 0);
    if (fd < 0) return false;

    int rc = sys_ioctl(fd, TIOCGWINSZ, &ws);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(ws.ws_col > 0);
    TEST_ASSERT(ws.ws_row > 0);
    sys_close(fd);
    return true;
}
