#include <tests/test.h>
#include <stdint.h>
#include <task/process.h>
#include <arch/x86_64/cpu.h>
#include <arch/x86_64/smp.h>
#include <drivers/terminal.h>
#include <arch/x86_64/apic.h>
#include <sys/syscall.h>
#include <mem/vmm.h>
#include <mem/pmm.h>
#include <lib/string.h>

// SMP scheduler sanity tests.
// Goal: prove that APs (cpu_index != 0) can actually execute runnable threads.

static volatile uint32_t g_seen_cpu_mask = 0;
static volatile int g_threads_completed = 0;

static bool smp_wait_for_any_ap_ready(uint32_t cpu_count)
{
    if (cpu_count <= 1)
        return true;

    for (int i = 0; i < 200000; i++)
    {
        for (uint32_t idx = 1; idx < cpu_count; idx++)
        {
            cpu_t* cpu = smp_get_cpu_by_index(idx);
            if (!cpu)
                continue;
            thread_t* active = __atomic_load_n(&cpu->active_thread, __ATOMIC_ACQUIRE);
            if (active)
                return true;
        }
        yield();
    }

    return false;
}

static void smp_worker_entry(void)
{
    cpu_t* cpu = get_cpu();
    if (cpu)
    {
        const uint32_t idx = (cpu->cpu_index >= 0 && cpu->cpu_index < 32) ? (uint32_t)cpu->cpu_index : 0;
        __atomic_fetch_or(&g_seen_cpu_mask, (1u << idx), __ATOMIC_SEQ_CST);
    }

    __atomic_fetch_add(&g_threads_completed, 1, __ATOMIC_SEQ_CST);
}

TEST_PRIO(test_smp_aps_execute_threads, 5)
{
    const uint32_t cpu_count = smp_get_cpu_count();

    // If only BSP present, nothing to prove.
    if (cpu_count <= 1)
        return true;

    if (!smp_wait_for_any_ap_ready(cpu_count))
    {
        printk("SMP sched: APs not online before test (cpu_count=%u)\n", cpu_count);
        return false;
    }

    g_seen_cpu_mask = 0;
    g_threads_completed = 0;

    // Create a bunch of short-lived threads to increase scheduling opportunities.
    // We intentionally create more threads than CPUs.
    const int num_threads = (int)(cpu_count * 4);

    process_t* p = process_create("smp_sched");
    TEST_ASSERT(p != nullptr);

    for (int i = 0; i < num_threads; i++)
    {
        thread_t* t = thread_create(p, smp_worker_entry, false);
        if (!t)
        {
            process_destroy(p);
            return false;
        }
    }

    // Wake APs and encourage immediate load balancing.
    apic_send_ipi_all_excluding_self(IPI_RESCHEDULE_VECTOR);

    // Wait for all threads to complete.
    // Bound by iterations so tests don't hang.
    for (int i = 0; i < 200000; i++)
    {
        const int completed = (int)__atomic_load_n(&g_threads_completed, __ATOMIC_SEQ_CST);
        if (completed >= num_threads)
            break;
        yield();
    }

    const int done = __atomic_load_n(&g_threads_completed, __ATOMIC_SEQ_CST);
    if (done < num_threads)
    {
        printk("SMP sched: only %d/%d threads completed\n", done, num_threads);
        process_destroy(p);
        return false;
    }

    const uint32_t mask = __atomic_load_n(&g_seen_cpu_mask, __ATOMIC_SEQ_CST);

    // Require that at least one AP ran at least one thread.
    // (Mask bit 0 is BSP; any other bit implies AP.)
    const bool ap_ran = (mask & ~1u) != 0;
    if (!ap_ran)
    {
        printk("SMP sched: threads only observed on BSP (mask=0x%x, cpu_count=%u)\n", mask, cpu_count);
        process_destroy(p);
        return false;
    }

    for (int i = 0; i < 1000; i++)
        yield();
    process_destroy(p);
    return true;
}

static const uint8_t smp_user_exit_stub[] = {
    0xB8, 0x03, 0x00, 0x00, 0x00, // mov eax, 3 (SYS_EXIT)
    0xBF, 0x00, 0x00, 0x00, 0x00, // mov edi, 0
    0x0F, 0x05 // syscall
};

static volatile uint32_t g_user_cpu_mask = 0;
static volatile int g_user_exit_count = 0;

static void smp_user_exit_hook([[maybe_unused]] int code)
{
    cpu_t* cpu = get_cpu();
    uint32_t idx = 0;
    if (cpu && cpu->cpu_index >= 0 && cpu->cpu_index < 32)
        idx = (uint32_t)cpu->cpu_index;
    __atomic_fetch_or(&g_user_cpu_mask, (1u << idx), __ATOMIC_SEQ_CST);
    __atomic_fetch_add(&g_user_exit_count, 1, __ATOMIC_SEQ_CST);
}

static void smp_enter_user_mode(uint64_t rip, uint64_t rsp)
{
    constexpr uint64_t user_cs = 0x20 | 3;
    constexpr uint64_t user_ss = 0x18 | 3;
    constexpr uint64_t rflags = 0x202;

    __asm__ volatile(
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
        "iretq\n"
        :
        : "r"(user_ss), "r"(rsp), "r"(rflags), "r"(user_cs), "r"(rip)
        : "memory", "rax", "rdx");
    __builtin_unreachable();
}

static void smp_user_thread_entry(void)
{
    for (;;)
    {
        thread_t* t = get_current_thread();
        if (t && t->user_entry && t->user_stack)
        {
            smp_enter_user_mode(t->user_entry, t->user_stack);
        }
        yield();
    }
}

static process_t* smp_create_user_exit_process(void)
{
    constexpr uint64_t user_base = 0x400000;
    pml4_t pml4 = vmm_new_pml4();
    if (!pml4)
        return nullptr;

    void* phys_page = pmm_alloc_page();
    if (!phys_page)
    {
        vmm_destroy_pml4(pml4);
        return nullptr;
    }

    vmm_map_page(pml4, user_base, (uint64_t)phys_page, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    void* virt_page = (void*)((uint64_t)phys_page + g_hhdm_offset);
    memcpy(virt_page, smp_user_exit_stub, sizeof(smp_user_exit_stub));

    process_t* proc = process_create("smp_user_exit");
    if (!proc)
    {
        vmm_destroy_pml4(pml4);
        return nullptr;
    }
    proc->pml4 = pml4;
    proc->parent = current_process;

    thread_t* t = thread_create(proc, smp_user_thread_entry, true);
    if (!t)
    {
        process_destroy(proc);
        return nullptr;
    }

    t->user_entry = user_base;
    t->user_stack = user_base + PAGE_SIZE - 16;

    return proc;
}

TEST_PRIO(test_smp_user_threads_execute_on_any_cpu, 6)
{
    const uint32_t cpu_count = smp_get_cpu_count();
    if (cpu_count <= 1)
        return true;

    if (!smp_wait_for_any_ap_ready(cpu_count))
    {
        printk("SMP user: APs not online before test (cpu_count=%u)\n", cpu_count);
        return false;
    }

    g_user_cpu_mask = 0;
    g_user_exit_count = 0;

    int num_threads = (int)(cpu_count * 4);
    if (num_threads > 32)
        num_threads = 32;

    process_t* procs[32] = {nullptr};

    syscall_set_exit_hook(smp_user_exit_hook);

    for (int i = 0; i < num_threads; i++)
    {
        procs[i] = smp_create_user_exit_process();
        if (!procs[i])
        {
            syscall_set_exit_hook(nullptr);
            for (int j = 0; j < i; j++)
            {
                if (procs[j])
                    process_destroy(procs[j]);
            }
            return false;
        }
    }

    apic_send_ipi_all_excluding_self(IPI_RESCHEDULE_VECTOR);

    for (int i = 0; i < 200000; i++)
    {
        const int done = __atomic_load_n(&g_user_exit_count, __ATOMIC_SEQ_CST);
        if (done >= num_threads)
            break;
        yield();
    }

    const int done = __atomic_load_n(&g_user_exit_count, __ATOMIC_SEQ_CST);
    const uint32_t mask = __atomic_load_n(&g_user_cpu_mask, __ATOMIC_SEQ_CST);

    syscall_set_exit_hook(nullptr);

    for (int i = 0; i < 1000; i++)
        yield();

    for (int i = 0; i < num_threads; i++)
    {
        if (procs[i])
            process_destroy(procs[i]);
    }

    if (done < num_threads)
    {
        printk("SMP user: only %d/%d user exits observed\n", done, num_threads);
        return false;
    }

    if ((mask & ~1u) == 0)
    {
        printk("SMP user: user exits only on BSP (mask=0x%x)\n", mask);
        return false;
    }

    return true;
}
