#pragma once

#include <stdint.h>

struct timeval
{
    int64_t tv_sec;  // seconds
    int64_t tv_usec; // microseconds
};

struct timezone
{
    int tz_minuteswest; // minutes west of Greenwich
    int tz_dsttime;     // daylight savings flag
};

int gettimeofday(struct timeval *tv, struct timezone *tz);
