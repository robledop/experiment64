#pragma once

#include <stdint.h>

typedef void (*sighandler_t)(int);
typedef uint64_t sigset_t;

#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)

// Hangup detected on controlling terminal or session ended.
#define SIGHUP 1
// Interrupt from keyboard (Ctrl+C).
#define SIGINT 2
// Quit from the keyboard (Ctrl+\\), often with core dump.
#define SIGQUIT 3
// Illegal instruction.
#define SIGILL 4
// Trap or breakpoint.
#define SIGTRAP 5
// Abort signal from abort().
#define SIGABRT 6
// Bus error (bad memory access).
#define SIGBUS 7
// Floating point exception.
#define SIGFPE 8
// Kill signal (cannot be caught or ignored).
#define SIGKILL 9
// User-defined signal 1.
#define SIGUSR1 10
// Segmentation fault.
#define SIGSEGV 11
// User-defined signal 2.
#define SIGUSR2 12
// Broken pipe: write to pipe with no readers.
#define SIGPIPE 13
// Alarm clock signal.
#define SIGALRM 14
// Termination request.
#define SIGTERM 15
// Child stopped or terminated.
#define SIGCHLD 17
// Continue if stopped.
#define SIGCONT 18
// Stop the process (cannot be caught or ignored).
#define SIGSTOP 19
// Stop from keyboard (Ctrl+Z).
#define SIGTSTP 20
// Background read from controlling terminal.
#define SIGTTIN 21
// Background write to controlling terminal.
#define SIGTTOU 22

#define SIG_MAX 32

// Provide behavior compatible with BSD signal semantics by making certain
// system calls restartable across signals. (not yet implemented)
#define SA_RESTART 0x1
// If signum is SIGCHLD, do not transform children into zombies when they terminate.
#define SA_NOCLDWAIT 0x2

typedef struct sigaction
{
    sighandler_t sa_handler;
    sigset_t sa_mask;
    uint64_t sa_flags;
    void (*sa_restorer)(void);
} sigaction_t;

typedef struct sigcontext
{
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rbp;
    uint64_t rax;
    uint64_t rip;
    uint64_t rflags;
    uint64_t rsp;
    sigset_t sigmask;
} sigcontext_t;