#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "vmm.h"
#include "cpu.h"

#define PROCESS_NAME_MAX 64
#define MAX_FDS 16
#define TIME_SLICE_MS 50

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

#include "vfs.h"
#include "spinlock.h"
#include "list.h"

typedef struct
{
    struct vfs_inode *inode;
    uint64_t offset;
} file_descriptor_t;

typedef struct Process
{
    int pid;
    char name[PROCESS_NAME_MAX];
    pml4_t pml4;            // Page directory (physical address)
    list_head_t threads;    // Head of thread list
    list_head_t list;       // Global process list node
    struct Process *parent; // Parent process
    int exit_code;
    bool terminated;
    uint64_t heap_end; // Current program break
    file_descriptor_t *fd_table[MAX_FDS];
    char cwd[VFS_MAX_PATH];
} process_t;

typedef struct Thread
{
    int tid;
    process_t *process;
    struct context *context; // Kernel stack pointer (saved context)
    thread_state_t state;
    uint64_t kstack_top;      // Kernel stack top
    uint64_t user_entry;      // For spawn
    uint64_t user_stack;      // For spawn
    uint64_t saved_user_rsp;  // Saved user RSP during syscalls
    fpu_state_t fpu_state;    // FPU/SSE state
    uint64_t sleep_until;     // Wake tick for sleep syscall
    void *chan;               // Sleep channel
    bool is_idle;             // Is this the idle thread?
    uint64_t ticks_remaining; // Time slice remaining
    uint64_t _align[2];       // Padding to ensure list is 16-byte aligned relative to start
    list_head_t list;         // Thread list node
} thread_t;

extern list_head_t process_list;
extern process_t *kernel_process;
extern spinlock_t scheduler_lock;
// extern process_t *current_process; // Removed
// extern thread_t *current_thread;   // Removed
extern volatile uint64_t scheduler_ticks;

void process_init(void);
process_t *process_create(const char *name);
void process_destroy(process_t *process);
void process_copy_fds(process_t *dest, const process_t *src);
thread_t *thread_create(process_t *process, void (*entry)(void), bool is_user);
thread_t *get_current_thread(void);
process_t *get_current_process(void);

#define current_thread (get_current_thread())
#define current_process (get_current_process())

bool scheduler_tick(void);

void schedule(void);
void yield(void);
void thread_sleep(void *chan, spinlock_t *lock);
void thread_wakeup(void *chan);
void switch_to(thread_t *prev, thread_t *next);

void process_spawn_init(void);
void process_dump(void);
