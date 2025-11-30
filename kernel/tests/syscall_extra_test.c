#include "test.h"
#include "ioctl.h"
#include "time.h"
#include "mman.h"
#include "fcntl.h"
#include <stddef.h>
#include <stdint.h>

// Syscall entry points (non-static in syscall.c)
int sys_gettimeofday(struct timeval* tv, struct timezone* tz);
int sys_ioctl(int fd, int request, void* arg);
int sys_open(const char* path, int flags);
int sys_close(int fd);

TEST(test_sys_gettimeofday_basic)
{
    struct timeval tv = {0};
    struct timezone tz = {0};
    int rc = sys_gettimeofday(&tv, &tz);
    TEST_ASSERT(rc == 0);
    // Should return some non-negative time; accept zero if clock not initialized yet.
    TEST_ASSERT(tv.tv_usec >= 0);
    TEST_ASSERT(tv.tv_usec < 1000000);
    TEST_ASSERT(tv.tv_sec >= 0);
    TEST_ASSERT(tz.tz_minuteswest == 0);
    TEST_ASSERT(tz.tz_dsttime == 0);
    return true;
}

TEST(test_sys_ioctl_tiocgwinsz)
{
    struct winsize ws = {0};
    int fd = sys_open("/dev/console", 0);
    if (fd < 0)
    {
        // Device not present in this test environment; skip.
        return true;
    }

    int rc = sys_ioctl(fd, TIOCGWINSZ, &ws);
    TEST_ASSERT(rc == 0);
    TEST_ASSERT(ws.ws_col > 0);
    TEST_ASSERT(ws.ws_row > 0);
    sys_close(fd);
    return true;
}
