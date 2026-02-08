#include <sys/syscall.h>
#include <syscall_common.h>
#include <lib/util.h>
#include <arch/x86_64/cpu.h>

static constexpr uint64_t THREAD_STACK_PAGES      = 4;
static constexpr uint64_t THREAD_STACK_SIZE       = THREAD_STACK_PAGES * PAGE_SIZE;
static constexpr uint64_t THREAD_GUARD_PAGES      = 1;
static constexpr uint64_t THREAD_GUARD_SIZE       = THREAD_GUARD_PAGES * PAGE_SIZE;
static constexpr uint64_t THREAD_STACK_TOTAL_SIZE = THREAD_STACK_SIZE + THREAD_GUARD_SIZE;
static constexpr uint64_t THREAD_STACK_TOP_HINT   = 0x7FFFFFFFF000ull;
static constexpr uint64_t THREAD_STACK_LOW_LIMIT  = 0x0000000000400000ull;

static inline uint64_t align_down_u64(uint64_t val, uint64_t align)
{
    return val & ~(align - 1);
}

/**
 * Searches downward from a hint for a page-aligned, non-overlapping stack region of a given size in the process’s VM map.
 * @param proc Process for which to find a stack region
 * @param size Size of the stack region to find
 * @param top_hint Hint for the top of the stack region
 * @param out_start Output parameter for the start address of the found stack region
 * @param out_end Output parameter for the end address of the found stack region
 * @return True if a stack region was found, false otherwise
 */
static bool find_stack_range(process_t *proc, uint64_t size, uint64_t top_hint, uint64_t *out_start, uint64_t *out_end)
{
    if (!proc || !out_start || !out_end)
        return false;

    uint64_t user_top = g_hhdm_offset ? g_hhdm_offset : 0x0000800000000000ull;
    if (user_top <= PAGE_SIZE)
        return false;

    const uint64_t hint = top_hint != 0 ? top_hint : THREAD_STACK_TOP_HINT;
    uint64_t limit      = min(hint, user_top - PAGE_SIZE);
    limit               = align_down_u64(limit, PAGE_SIZE);

    while (limit >= THREAD_STACK_LOW_LIMIT + size) {
        const uint64_t start = limit - size;
        const uint64_t end   = limit;
        bool overlap         = false;
        uint64_t next_end    = limit;

        spinlock_acquire(&proc->vm_lock);
        vm_area_t *area;
        list_foreach_entry(area, &proc->vm_areas, list) {
            if (!(end <= area->start || start >= area->end)) {
                overlap = true;
                if (area->start < next_end)
                    next_end = area->start;
            }
        }
        spinlock_release(&proc->vm_lock);

        if (!overlap) {
            *out_start = start;
            *out_end   = end;
            return true;
        }

        if (next_end >= limit)
            break;
        limit = align_down_u64(next_end, PAGE_SIZE);
    }

    return false;
}

/**
 * Prepares a clean user-mode context and uses iretq to transition from kernel mode
 * to user mode, passing one argument in rdi and starting at t->user_entry with t->user_stack.
 */
static void thread_user_trampoline(void)
{
    constexpr uint64_t user_cs = 0x20 | 3;
    constexpr uint64_t user_ss = 0x18 | 3;
    constexpr uint64_t rflags  = 0x202;

    thread_t *t = current_thread;
    if (!t || !t->user_entry || !t->user_stack) {
        sys_exit(-1);
        __builtin_unreachable();
    }

    const uint64_t entry = t->user_entry;
    const uint64_t stack = t->user_stack;
    const uint64_t arg   = t->user_arg;

    __asm__ volatile (
        "cli\n"
        "swapgs\n"
        "mov ds, %0\n"
        "mov es, %0\n"
        "mov fs, %0\n"
        "mov gs, %0\n"
        "push %0\n"
        "push %1\n"
        "push %2\n"
        "push %3\n"
        "push %4\n"
        "mov rdi, %5\n"
        "xor rsi, rsi\n"
        "iretq\n"
        :: "r"(user_ss), "r"(stack), "r"(rflags), "r"(user_cs), "r"(entry), "r"(arg)
        : "memory", "rdi", "rsi"
    );

    __builtin_unreachable();
}

int sys_thread_create(uint64_t entry, uint64_t arg)
{
    if (!current_thread || !current_thread->is_user)
        return -1;
    if (entry == 0)
        return -1;
    if (!user_ptr_read_ok((void *)entry, 1, "sys_thread_create entry"))
        return -1;

    uint64_t top_hint = THREAD_STACK_TOP_HINT;
    cpu_t *cpu        = get_cpu();
    if (cpu && cpu->user_rsp) {
        uint64_t rsp_hint = align_down_u64(cpu->user_rsp, PAGE_SIZE);
        if (rsp_hint > THREAD_STACK_LOW_LIMIT + THREAD_STACK_TOTAL_SIZE)
            top_hint = min(top_hint, rsp_hint - THREAD_STACK_TOTAL_SIZE);
    }

    uint64_t range_start = 0;
    uint64_t range_end   = 0;
    if (!find_stack_range(current_process, THREAD_STACK_TOTAL_SIZE, top_hint, &range_start, &range_end)) {
        return -1;
    }

    uint64_t guard_start = range_start;
    uint64_t stack_start = guard_start + THREAD_GUARD_SIZE;
    uint64_t stack_end   = range_end;

    constexpr uint32_t stack_vma_flags = VMA_READ | VMA_WRITE | VMA_USER | VMA_STACK | VMA_ANON;
    if (!map_user_anonymous_range(current_process,
                                  current_process->pml4,
                                  stack_start,
                                  THREAD_STACK_SIZE,
                                  stack_vma_flags)) {
        return -1;
    }

    constexpr uint32_t guard_vma_flags = VMA_USER | VMA_STACK | VMA_ANON;
    if (!vm_area_add(current_process, guard_start, stack_start, guard_vma_flags)) {
        sys_munmap((void *)stack_start, THREAD_STACK_SIZE);
        return -1;
    }

    thread_t *thread = thread_create(current_process, thread_user_trampoline, true);
    if (!thread) {
        sys_munmap((void *)guard_start, THREAD_STACK_TOTAL_SIZE);
        return -1;
    }

    // Thread entry is reached via iretq (no call frame), keep %rsp 8 mod 16 for SysV ABI.
    uint64_t user_stack = align_down_u64(stack_end, 16) - 8;

    thread->user_entry      = entry;
    thread->user_stack      = user_stack;
    thread->user_arg        = arg;
    thread->saved_user_rsp  = user_stack;
    thread->user_stack_base = guard_start;
    thread->user_stack_top  = stack_end;
    thread_make_ready(thread);

    TEST_SYSCALL_LOG("sys_thread_create: pid=%d tid=%d entry=0x%lx stack=0x%lx arg=0x%lx\n",
                     current_process->pid,
                     thread->tid,
                     (unsigned long)entry,
                     (unsigned long)user_stack,
                     (unsigned long)arg);

    return thread->tid;
}