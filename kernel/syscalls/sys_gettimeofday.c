#include <syscall_common.h>

#include <drivers/tsc.h>
#include <sys/time.h>

int sys_gettimeofday(struct timeval* tv, struct timezone* tz)
{
    const uint64_t ns = tsc_monotonic_ns();

    if (tv)
    {
        if (!user_ptr_write_ok(tv, sizeof(*tv), "sys_gettimeofday"))
            return -1;
        tv->tv_sec = (int64_t)(ns / 1000000000ull);
        tv->tv_usec = (int64_t)((ns % 1000000000ull) / 1000ull);
    }
    if (tz)
    {
        if (!user_ptr_write_ok(tz, sizeof(*tz), "sys_gettimeofday"))
            return -1;
        tz->tz_minuteswest = 0;
        tz->tz_dsttime = 0;
    }
    return 0;
}
