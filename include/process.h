#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "vmm.h"
#include "cpu.h"

#define PROCESS_NAME_MAX 64

typedef enum
{
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_TERMINATED
} thread_state_t;

struct Thread;

// Saved registers for kernel context switches.
// Matches the pushes in switch.S
struct context
{
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t rip;
};

typedef struct Process
{
    int pid;
    char name[PROCESS_NAME_MAX];
    pml4_t pml4;            // Page directory (physical address)
    struct Thread *threads; // Head of thread list
    struct Process *next;   // Global process list
    struct Process *parent; // Parent process
    int exit_code;
    bool terminated;
} process_t;

typedef struct Thread
{
    int tid;
    process_t *process;
    struct context *context; // Kernel stack pointer (saved context)
    thread_state_t state;
    uint64_t kstack_top;     // Kernel stack top
    uint64_t user_entry;     // For spawn
    uint64_t user_stack;     // For spawn
    uint64_t saved_user_rsp; // Saved user RSP during syscalls
    fpu_state_t fpu_state;   // FPU/SSE state
    struct Thread *next;
} thread_t;

extern process_t *process_list;
extern process_t *current_process;
extern thread_t *current_thread;

void process_init(void);
process_t *process_create(const char *name);
thread_t *thread_create(process_t *process, void (*entry)(void), bool is_user);
thread_t *get_current_thread(void);

void schedule(void);
void yield(void);
void switch_to(thread_t *prev, thread_t *next);
