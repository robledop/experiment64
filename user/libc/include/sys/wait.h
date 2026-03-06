#pragma once

#define WNOHANG 0x1 // Return immediately if no child has exited.

#define WIFEXITED(status) ((status) >= 0 && (status) < 128) // True when exited normally.
#define WEXITSTATUS(status) (status) // Exit code when WIFEXITED is true.
#define WIFSIGNALED(status) ((status) >= 128) // True when terminated by a signal.
#define WTERMSIG(status) ((status) - 128) // Signal number when WIFSIGNALED is true.
#define WCOREDUMP(status) 0
