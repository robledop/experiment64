#include <tests/test.h>
#include <sys/ioctl.h>
#include <sys/signal.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/termios.h>
#include <lib/string.h>
#include <status.h>
#include <task/process.h>
#include <drivers/keyboard.h>
#include <drivers/tsc.h>

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

TEST(test_sys_thread_syscall_errors)
{
    int pid = sys_spawn("/tests/thread_syscall_error_test");
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

TEST(test_sys_stat_mode_compat)
{
    int pid = sys_spawn("/tests/stat_mode_test");
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

TEST(test_sys_ioctl_error_codes)
{
    struct winsize ws = {0};
    TEST_ASSERT(sys_ioctl(-1, TIOCGWINSZ, &ws) == -EBADF);

    int fd = sys_open("/dev/console", 0);
    TEST_ASSERT(fd >= 0);
    TEST_ASSERT(sys_ioctl(fd, 0x7FFFFFFF, &ws) == -ENOTTY);
    sys_close(fd);
    return true;
}

TEST(test_sys_console_termios_vmin_vtime)
{
    int fd = sys_open("/dev/console", 0);
    TEST_ASSERT(fd >= 0);

    struct termios old_t = {0};
    TEST_ASSERT(sys_ioctl(fd, TIOCGETA, &old_t) == 0);

    struct termios new_t = old_t;
    new_t.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    new_t.c_cc[VMIN] = 0;
    new_t.c_cc[VTIME] = 1;
    TEST_ASSERT(sys_ioctl(fd, TCSETSW, &new_t) == 0);

    char c = 0;
    uint64_t start = tsc_nanos();
    TEST_ASSERT(sys_read(fd, &c, 1) == 0);
    uint64_t end = tsc_nanos();
    if (start > 0 && end >= start) {
        uint64_t elapsed_ms = (end - start) / 1000000ull;
        TEST_ASSERT(elapsed_ms >= 50);
        TEST_ASSERT(elapsed_ms < 1000);
    }

    keyboard_inject_scancode(0x01); // ESC
    TEST_ASSERT(sys_read(fd, &c, 1) == 1);
    TEST_ASSERT(c == 27);

    TEST_ASSERT(sys_ioctl(fd, TCSETSF, &old_t) == 0);
    sys_close(fd);
    return true;
}

TEST(test_sys_openpty_data_path)
{
    int fds[2] = {-1, -1};
    TEST_ASSERT(sys_openpty(fds) == 0);
    TEST_ASSERT(fds[0] >= 0);
    TEST_ASSERT(fds[1] >= 0);

    const char in_a[] = "abc";
    char out_a[4] = {0};
    TEST_ASSERT(sys_write(fds[0], in_a, 3) == 3);
    TEST_ASSERT(sys_read(fds[1], out_a, 3) == 3);
    TEST_ASSERT(memcmp(in_a, out_a, 3) == 0);

    const char in_b[] = "xyz";
    char out_b[4] = {0};
    TEST_ASSERT(sys_write(fds[1], in_b, 3) == 3);
    TEST_ASSERT(sys_read(fds[0], out_b, 3) == 3);
    TEST_ASSERT(memcmp(in_b, out_b, 3) == 0);

    sys_close(fds[0]);
    sys_close(fds[1]);
    return true;
}

TEST(test_sys_openpty_termios_vmin_vtime)
{
    int fds[2] = {-1, -1};
    TEST_ASSERT(sys_openpty(fds) == 0);

    struct termios old_t = {0};
    TEST_ASSERT(sys_ioctl(fds[1], TIOCGETA, &old_t) == 0);

    struct termios new_t = old_t;
    new_t.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    new_t.c_cc[VMIN] = 0;
    new_t.c_cc[VTIME] = 1;
    TEST_ASSERT(sys_ioctl(fds[1], TCSETSW, &new_t) == 0);

    char c = 0;
    uint64_t start = tsc_nanos();
    TEST_ASSERT(sys_read(fds[1], &c, 1) == 0);
    uint64_t end = tsc_nanos();
    if (start > 0 && end >= start) {
        uint64_t elapsed_ms = (end - start) / 1000000ull;
        TEST_ASSERT(elapsed_ms >= 50);
        TEST_ASSERT(elapsed_ms < 1000);
    }

    const char esc = 27;
    TEST_ASSERT(sys_write(fds[0], &esc, 1) == 1);
    TEST_ASSERT(sys_read(fds[1], &c, 1) == 1);
    TEST_ASSERT(c == esc);

    TEST_ASSERT(sys_ioctl(fds[1], TCSETSF, &old_t) == 0);
    sys_close(fds[0]);
    sys_close(fds[1]);
    return true;
}

TEST(test_sys_openpty_sigint_foreground_pid)
{
    int fds[2] = {-1, -1};
    TEST_ASSERT(sys_openpty(fds) == 0);

    int pid = sys_spawn("/tests/sigtest_int");
    TEST_ASSERT(pid > 1);

    TEST_ASSERT(sys_ioctl(fds[1], TIOCSPGRP, &pid) == 0);
    char ctrl_c = 0x03;
    TEST_ASSERT(sys_write(fds[0], &ctrl_c, 1) == 1);

    int status = -1;
    TEST_ASSERT(sys_waitpid(pid, &status, 0) == pid);
    TEST_ASSERT(status == 128 + SIGINT);

    sys_close(fds[0]);
    sys_close(fds[1]);
    return true;
}

TEST(test_sys_openpty_tiocgpgrp_roundtrip)
{
    int fds[2] = {-1, -1};
    TEST_ASSERT(sys_openpty(fds) == 0);

    int set_pid = 12345;
    TEST_ASSERT(sys_ioctl(fds[1], TIOCSPGRP, &set_pid) == 0);

    int got_pid = -1;
    TEST_ASSERT(sys_ioctl(fds[0], TIOCGPGRP, &got_pid) == 0);
    TEST_ASSERT(got_pid == set_pid);

    sys_close(fds[0]);
    sys_close(fds[1]);
    return true;
}

TEST(test_sys_openpty_shell_smoke)
{
    int pid = sys_spawn("/tests/pty_shell_test");
    TEST_ASSERT(pid > 1);

    int status = -1;
    TEST_ASSERT(sys_waitpid(pid, &status, 0) == pid);
    TEST_ASSERT(status == 0);
    return true;
}

TEST(test_sys_pipe_fionbio_nonblocking_write)
{
    int pipefd[2] = {-1, -1};
    TEST_ASSERT(sys_pipe(pipefd) == 0);

    // Set non-blocking on write end
    int one = 1;
    TEST_ASSERT(sys_ioctl(pipefd[1], FIONBIO, &one) == 0);

    // Fill the pipe buffer (PIPE_BUF_SIZE = 4096)
    char fill[256];
    memset(fill, 'A', sizeof(fill));
    int total_written = 0;
    for (int i = 0; i < 16; i++) {
        int w = sys_write(pipefd[1], fill, sizeof(fill));
        if (w > 0)
            total_written += w;
    }
    TEST_ASSERT(total_written == 4096);

    // Next write should return 0 (non-blocking, pipe full)
    int w = sys_write(pipefd[1], fill, sizeof(fill));
    TEST_ASSERT(w == 0);

    // Drain some data so there's space
    char drain[128];
    int r = sys_read(pipefd[0], drain, sizeof(drain));
    TEST_ASSERT(r == 128);

    // Now a write should succeed (up to available space)
    w = sys_write(pipefd[1], fill, sizeof(fill));
    TEST_ASSERT(w == 128);

    sys_close(pipefd[0]);
    sys_close(pipefd[1]);
    return true;
}

TEST(test_sys_openpty_tiochup_revoke)
{
    int fds[2] = {-1, -1};
    TEST_ASSERT(sys_openpty(fds) == 0);

    // Write some data via master→slave direction
    const char msg[] = "hello";
    TEST_ASSERT(sys_write(fds[0], msg, 5) == 5);

    // Revoke the PTY
    TEST_ASSERT(sys_ioctl(fds[0], TIOCHUP, nullptr) == 0);

    // Buffered data is still returned, then EOF
    char buf[16] = {0};
    TEST_ASSERT(sys_read(fds[1], buf, sizeof(buf)) == 5);
    TEST_ASSERT(sys_read(fds[1], buf, sizeof(buf)) == 0);

    // Master read (empty ring) returns 0 immediately
    TEST_ASSERT(sys_read(fds[0], buf, sizeof(buf)) == 0);

    // Writes on both sides should return 0 (revoked)
    TEST_ASSERT(sys_write(fds[0], msg, 5) == 0);
    TEST_ASSERT(sys_write(fds[1], msg, 5) == 0);

    sys_close(fds[0]);
    sys_close(fds[1]);
    return true;
}
