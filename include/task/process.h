#pragma once
#include <stdint.h>
#include <fs/vfs.h>
#include <task/spinlock.h>
#include <lib/list.h>
#include <lib/path.h>
#include <mem/vmm.h>
#include <arch/x86_64/cpu.h>
#include <sys/signal.h>

#define KSTACK_SIZE 65536
#define KSTACK_SYSCALL_HEADROOM 512

#define PROCESS_NAME_MAX 64
#define MAX_FDS 32
#define TIME_SLICE_MS 50

#define VMA_READ (1u << 0)
#define VMA_WRITE (1u << 1)
#define VMA_EXEC (1u << 2)
#define VMA_USER (1u << 3)
#define VMA_MMAP (1u << 4)
#define VMA_STACK (1u << 5)
#define VMA_ANON (1u << 6)

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


typedef struct
{
    struct vfs_inode* inode;
    uint64_t offset;
    int flags;
    uint32_t ref; // Reference count for dup()
} file_descriptor_t;

typedef struct vm_area
{
    uint64_t start;
    uint64_t end;
    uint32_t flags;
    list_item_t list;
} vm_area_t;

typedef struct Process
{
    int pid;
    char name[PROCESS_NAME_MAX];
    pml4_t pml4; // Page directory (physical address)
    list_item_t threads; // Head of a thread list
    list_item_t list; // Global process list node
    struct Process* parent; // Parent process
    int exit_code;
    bool terminated;
    sigaction_t sigactions[SIG_MAX];
    sigset_t sig_mask;
    sigset_t sig_pending;
    int sig_inflight;
    uint64_t heap_end; // Current program break
    file_descriptor_t* fd_table[MAX_FDS];
    char cwd[PATH_MAX];
    list_item_t vm_areas; // List of vm_area_t
    uint32_t vm_area_count;
} process_t;

typedef struct Thread
{
    fpu_state_t fpu_state; // FPU/SSE state
    process_t* process;
    uint64_t rsp; // Saved kernel stack pointer for context switches
    struct context* context; // Base of saved context frame
    uint64_t kstack_top; // Kernel stack top
    uint64_t user_entry; // For spawn
    uint64_t user_stack; // For spawn
    uint64_t user_arg;
    uint64_t saved_user_rsp; // Saved user RSP during syscalls
    void* chan; // Sleep channel
    uint64_t ticks_remaining; // Time slice remaining
    uint64_t _align[2]; // Padding to keep list 16-byte aligned relative to start
    list_item_t list; // Thread list node
    int tid;
    thread_state_t state;
    int exit_code;
    bool is_idle; // Is this the idle thread?
    bool is_user; // Runs in user mode
} thread_t;

extern list_item_t process_list;
extern process_t* kernel_process;
extern process_t* init_process;
extern spinlock_t scheduler_lock;
extern volatile uint64_t scheduler_ticks;

void process_init(void);
process_t* process_create(const char* name);
void process_destroy(process_t* process);
void process_reap(process_t* process);
bool process_can_reap_locked(process_t* proc);
void process_copy_fds(process_t* dest, const process_t* src);
void vm_area_init(process_t* proc);
vm_area_t* vm_area_add(process_t* proc, uint64_t start, uint64_t end, uint32_t flags);
void vm_area_clone(process_t* dest, const process_t* src);
void vm_area_clear(process_t* proc);
thread_t* thread_create(process_t* process, void (*entry)(void), bool is_user);
void thread_make_ready(thread_t* thread);
thread_t* get_current_thread(void);
process_t* get_current_process(void);

#define current_thread (get_current_thread())
#define current_process (get_current_process())

bool scheduler_tick(void);

void schedule(void);
void yield(void);
void thread_sleep(void* chan, spinlock_t* lock);
void thread_wakeup(void* chan);
void switch_to(thread_t* prev, thread_t* next);

void process_spawn_init(void);
void process_dump(void);
