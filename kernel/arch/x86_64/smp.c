#include "smp.h"
#include "boot.h"
#include "terminal.h"
#include "cpu.h"
#include "gdt.h"
#include "idt.h"
#include "apic.h"
#include "vmm.h"
#include "syscall.h"
#include "spinlock.h"
#include <stdatomic.h>
#include "uart.h"

#define MAX_CPUS 32

static atomic_int cpus_started = 0;
static cpu_t cpus[MAX_CPUS];

static void ap_main(struct limine_smp_info *info)
{
    enable_sse();
    cpu_t *cpu = (cpu_t *)info->extra_argument;
    wrmsr(MSR_GS_BASE, (uint64_t)cpu);
    wrmsr(MSR_KERNEL_GS_BASE, (uint64_t)cpu);

    gdt_init();
    idt_reload();
    apic_local_init();
    syscall_init();

    atomic_fetch_add(&cpus_started, 1);

    __asm__ volatile("sti");

    while (1)
    {
        __asm__ volatile("hlt");
    }
}

void smp_init_cpu0(void)
{
    struct limine_smp_response *smp_response = boot_get_smp_response();
    if (smp_response == nullptr)
    {
        // If SMP response is missing, we can't set up GS_BASE, so gdt_init will crash.
        // Hang here to indicate failure.
        hcf();
        return;
    }

    bool bsp_found = false;
    for (uint64_t i = 0; i < smp_response->cpu_count; i++)
    {
        if (i >= MAX_CPUS)
            break;

        struct limine_smp_info *cpu_info = smp_response->cpus[i];

        if (cpu_info->lapic_id == smp_response->bsp_lapic_id)
        {
            cpus[i].lapic_id = (int)cpu_info->lapic_id;
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
    atomic_fetch_add(&cpus_started, 1);
}

void smp_boot_aps(void)
{
    struct limine_smp_response *smp_response = boot_get_smp_response();
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

        struct limine_smp_info *cpu_info = smp_response->cpus[i];

        if (cpu_info->lapic_id != smp_response->bsp_lapic_id)
        {
            cpus[i].lapic_id = (int)cpu_info->lapic_id;
            cpus[i].self = &cpus[i];
            cpus[i].active_thread = nullptr;

            cpu_info->extra_argument = (uint64_t)&cpus[i];
            cpu_info->goto_address = ap_main;
        }
    }

    boot_message(INFO, "SMP: Waiting for APs...");

    // Wait a bit for APs to start (very crude)
    for (volatile int i = 0; i < 10000000; i++)
        ;

    boot_message(INFO, "SMP: Started %d/%ld CPUs", atomic_load(&cpus_started), smp_response->cpu_count);
}
