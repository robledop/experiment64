#pragma once

#include <stdint.h>

typedef uint64_t rlim_t;

struct rlimit {
    rlim_t rlim_cur;
    rlim_t rlim_max;
};

#define RLIM_INFINITY ((rlim_t)-1)
#define RLIMIT_CORE   1
#define RLIMIT_DATA   2
#define RLIMIT_FSIZE  3
#define RLIMIT_NOFILE 4
#define RLIMIT_STACK  5

#define RUSAGE_SELF 0
#define RUSAGE_CHILDREN -1

struct rusage {
    long ru_utime_sec;
    long ru_utime_usec;
    long ru_stime_sec;
    long ru_stime_usec;
    long ru_maxrss;
    long ru_ixrss;
    long ru_idrss;
    long ru_isrss;
    long ru_minflt;
    long ru_majflt;
    long ru_nswap;
    long ru_inblock;
    long ru_oublock;
    long ru_msgsnd;
    long ru_msgrcv;
    long ru_nsignals;
    long ru_nvcsw;
    long ru_nivcsw;
};

int getrusage(int who, struct rusage *usage);
int getrlimit(int resource, struct rlimit *rlim);
int setrlimit(int resource, const struct rlimit *rlim);
