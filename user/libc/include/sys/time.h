#pragma once

#include <uapi/sys/time.h>

int gettimeofday(struct timeval *tv, struct timezone *tz);
int settimeofday(const struct timeval *tv, const struct timezone *tz);
int nanosleep(const struct timespec *req, struct timespec *rem);
