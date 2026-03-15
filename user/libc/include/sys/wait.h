#pragma once

#include <stdint.h>

#define WNOHANG 0x1 // Return immediately if no child has exited.

#define WIFEXITED(status) ((status) >= 0 && (status) < 128) // True when exited normally.
#define WEXITSTATUS(status) (status) // Exit code when WIFEXITED is true.
#define WIFSIGNALED(status) ((status) >= 128) // True when terminated by a signal.
#define WTERMSIG(status) ((status) - 128) // Signal number when WIFSIGNALED is true.
#define WCOREDUMP(status) 0

#define MAX_CRASH_FRAMES 16

typedef struct
{
    uint64_t fault_rip;
    uint64_t frames[MAX_CRASH_FRAMES];
    int frame_count;
} crash_info_t;

int wait4(int pid, int *status, int options, crash_info_t *info);
