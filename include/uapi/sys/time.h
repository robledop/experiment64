#pragma once

#include <stdint.h>

struct timespec
{
    int64_t tv_sec;
    long tv_nsec;
};

struct timeval
{
    int64_t tv_sec;
    int64_t tv_usec;
};

struct timezone
{
    int tz_minuteswest;
    int tz_dsttime;
};
