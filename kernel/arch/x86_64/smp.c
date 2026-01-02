#include <smp.h>
#include <boot.h>
#include <terminal.h>
#include <cpu.h>
#include <gdt.h>
#include <idt.h>
#include <apic.h>
#include <syscall.h>
#include <process.h>

#define MAX_CPUS 32

static volatile int cpus_started = 0;
static volatile bool ap_scheduler_ready = false;
static cpu_t cpus[MAX_CPUS];
static uint32_t cpu_count = 1;
static uint32_t bsp_lapic_id = 0;

[[noreturn]]
static void ap_main(struct limine_smp_info* info)
{
    enable_simd();
    cpu_t* cpu = (cpu_t*)info->extra_argument;
    wrmsr(MSR_GS_BASE, (uint64_t)cpu);
    wrmsr(MSR_KERNEL_GS_BASE, (uint64_t)cpu);

    gdt_init();
    idt_reload();
    apic_local_init();
    syscall_init();

    __atomic_fetch_add(&cpus_started, 1, __ATOMIC_SEQ_CST);

    // Wait for BSP to initialize the scheduler and create idle threads
    while (!__atomic_load_n(&ap_scheduler_ready, __ATOMIC_SEQ_CST))
    {
        __asm__ volatile("pause");
    }

    // Initialize this AP's scheduler state (set idle thread as active)
    smp_init_ap_scheduler();

    __asm__ volatile("sti");

    // Enter the idle loop - timer interrupts will trigger schedule()
    while (1)
    {
        __asm__ volatile("hlt");
    }
}

void smp_init_cpu0(void)
{
    struct limine_smp_response* smp_response = boot_get_smp_response();
    if (smp_response == nullptr)
    {
        // If the SMP response is missing, we can't set up GS_BASE, so gdt_init will crash.
        hcf();
    }

    bsp_lapic_id = smp_response->bsp_lapic_id;
    cpu_count = (uint32_t)(smp_response->cpu_count > MAX_CPUS ? MAX_CPUS : smp_response->cpu_count);

    bool bsp_found = false;
    for (uint64_t i = 0; i < smp_response->cpu_count; i++)
    {
        if (i >= MAX_CPUS)
            break;

        struct limine_smp_info* cpu_info = smp_response->cpus[i];

        if (cpu_info->lapic_id == smp_response->bsp_lapic_id)
        {
            cpus[i].lapic_id = (int)cpu_info->lapic_id;
            cpus[i].cpu_index = (int)i;
            cpus[i].self = &cpus[i];
            cpus[i].active_thread = nullptr;

            // Load null selector into GS/FS to ensure MSR_GS_BASE is used
            __asm__ volatile("xor eax, eax; mov gs, eax; mov fs, eax" ::: "eax");

            wrmsr(MSR_GS_BASE, (uint64_t)&cpus[i]);
            wrmsr(MSR_KERNEL_GS_BASE, (uint64_t)&cpus[i]);
            bsp_found = true;
            break;
        }
    }

    if (!bsp_found)
    {
        hcf();
    }
    __atomic_fetch_add(&cpus_started, 1, __ATOMIC_SEQ_CST);
}

void smp_boot_aps(void)
{
    struct limine_smp_response* smp_response = boot_get_smp_response();
    if (smp_response == nullptr)
    {
        boot_message(WARNING, "SMP: No response found");
        return;
    }

    boot_message(INFO, "SMP: Found %ld CPUs", smp_response->cpu_count);

    if (smp_response->cpu_count > MAX_CPUS)
    {
        boot_message(WARNING, "SMP: CPU count %ld exceeds MAX_CPUS %d", smp_response->cpu_count, MAX_CPUS);
    }

    for (uint64_t i = 0; i < smp_response->cpu_count; i++)
    {
        if (i >= MAX_CPUS)
            break;

        struct limine_smp_info* cpu_info = smp_response->cpus[i];

        if (cpu_info->lapic_id != smp_response->bsp_lapic_id)
        {
            cpus[i].lapic_id = (int)cpu_info->lapic_id;
            cpus[i].cpu_index = (int)i;
            cpus[i].self = &cpus[i];
            cpus[i].active_thread = nullptr;

            cpu_info->extra_argument = (uint64_t)&cpus[i];
            cpu_info->goto_address = ap_main;
        }
    }

    boot_message(INFO, "SMP: Waiting for APs...");

    // Wait a bit for APs to start
    for (volatile int i = 0; i < 10000000; i++)
    {
    }

    boot_message(INFO, "SMP: Started %d/%ld CPUs", __atomic_load_n(&cpus_started, __ATOMIC_SEQ_CST),
                 smp_response->cpu_count);
}

void smp_ap_scheduler_ready(void)
{
    __atomic_store_n(&ap_scheduler_ready, true, __ATOMIC_SEQ_CST);
}

uint32_t smp_get_cpu_count(void)
{
    return cpu_count;
}

bool smp_is_bsp(void)
{
    return apic_get_lapic_id() == bsp_lapic_id;
}

