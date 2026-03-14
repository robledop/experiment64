#include <syscall_common.h>

#include <drivers/tsc.h>
#include <status.h>
#include <sys/time.h>

int sys_gettimeofday(struct timeval* tv, struct timezone* tz)
{
    const uint64_t ns = tsc_monotonic_ns();

    if (tv)
    {
        if (!user_ptr_write_ok(tv, sizeof(*tv), "sys_gettimeofday"))
            return -EFAULT;
        struct timeval tv_k = {
            .tv_sec  = (int64_t)(ns / 1000000000ull),
            .tv_usec = (int64_t)((ns % 1000000000ull) / 1000ull),
        };
        if (!copy_to_user(tv, &tv_k, sizeof(tv_k)))
            return -EFAULT;
    }
    if (tz)
    {
        if (!user_ptr_write_ok(tz, sizeof(*tz), "sys_gettimeofday"))
            return -EFAULT;
        struct timezone tz_k = {
            .tz_minuteswest = 0,
            .tz_dsttime     = 0,
        };
        if (!copy_to_user(tz, &tz_k, sizeof(tz_k)))
            return -EFAULT;
    }
    return 0;
}
