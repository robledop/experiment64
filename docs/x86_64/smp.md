# Symmetric Multiprocessing (`smp.c`)

SMP (Symmetric Multiprocessing) allows multiple CPU cores to run code simultaneously. This file handles the initialization of all CPU cores in the system.

---

## Terminology

| Term             | Meaning                                               |
|------------------|-------------------------------------------------------|
| **BSP**          | Bootstrap Processor — the first CPU that runs at boot |
| **AP**           | Application Processor — any CPU other than the BSP    |
| **LAPIC ID**     | Unique identifier for each CPU's Local APIC           |
| **SMP Response** | Limine bootloader's info about available CPUs         |

---

## Per-CPU Data Structure

Each CPU has its own `cpu_t` structure:

```c
typedef struct cpu {
    struct cpu *self;           // Self-pointer (for GS-based access)
    uint64_t user_rsp;          // Saved user stack pointer
    uint64_t kernel_rsp;        // Current kernel stack pointer
    struct Thread *active_thread;   // Currently running thread
    struct Thread *scheduler_thread; // Per-CPU scheduler context
    int lapic_id;                   // This CPU's LAPIC ID
    int cpu_index;                  // Index into cpus[] (0 = BSP)
    struct gdt_desc gdt[7];         // Per-CPU GDT
    struct tss_entry tss;           // Per-CPU TSS
} cpu_t;
```

The kernel maintains an array of these:

```c
#define MAX_CPUS 32
static cpu_t cpus[MAX_CPUS];
```

---

## GS Segment for Per-CPU Data

x86_64 uses the `GS` segment register for per-CPU data access:

```c
// Set GS base to point to this CPU's structure
wrmsr(MSR_GS_BASE, (uint64_t)&cpus[i]);
wrmsr(MSR_KERNEL_GS_BASE, (uint64_t)&cpus[i]);
```

The `self` pointer at offset 0 allows fast access:

```c
cpu_t* get_cpu(void) {
    cpu_t* cpu;
    __asm__ volatile("mov %0, gs:[0]" : "=r"(cpu));
    return cpu;
}
```

### Why Two MSRs?

| MSR                  | Purpose                             |
|----------------------|-------------------------------------|
| `MSR_GS_BASE`        | Current GS base (kernel mode)       |
| `MSR_KERNEL_GS_BASE` | Saved GS base (swapped on `swapgs`) |

When entering/exiting user mode, `swapgs` exchanges these values so the kernel always has access to per-CPU data.

---

## Boot Sequence

### 1. BSP Initialization (`smp_init_cpu0`)

Called early in boot on the BSP (CPU 0):

```c
void smp_init_cpu0(void)
{
    struct limine_smp_response* smp_response = boot_get_smp_response();
    if (!smp_response)
        hcf();

    bsp_lapic_id = smp_response->bsp_lapic_id;
    cpu_count = (uint32_t)(smp_response->cpu_count > MAX_CPUS ? MAX_CPUS : smp_response->cpu_count);

    for (uint64_t i = 0; i < smp_response->cpu_count; i++) {
        if (i >= MAX_CPUS)
            break;
        struct limine_smp_info* cpu_info = smp_response->cpus[i];

        if (cpu_info->lapic_id == smp_response->bsp_lapic_id) {
            cpus[i].lapic_id = (int)cpu_info->lapic_id;
            cpus[i].cpu_index = (int)i;
            cpus[i].self = &cpus[i];
            cpus[i].active_thread = nullptr;
            
            // Clear segment registers before setting MSR
            __asm__ volatile("xor eax, eax; mov gs, eax; mov fs, eax");
            
            wrmsr(MSR_GS_BASE, (uint64_t)&cpus[i]);
            wrmsr(MSR_KERNEL_GS_BASE, (uint64_t)&cpus[i]);
            break;
        }
    }
    
    __atomic_fetch_add(&cpus_started, 1, __ATOMIC_SEQ_CST);
}
```

**Why clear GS/FS first?**  
In 64-bit mode the FS/GS base comes from the MSRs, but clearing the selectors
avoids stale descriptors and gives a clean baseline before writing `MSR_GS_BASE`
and `MSR_KERNEL_GS_BASE`.

---

### 2. AP Startup (`smp_boot_aps`)

Called after APIC/TSC init but before `process_init()`. APs will start and then
wait for the scheduler-ready flag:

```c
void smp_boot_aps(void)
{
    struct limine_smp_response* smp_response = boot_get_smp_response();
    if (!smp_response) {
        boot_message(WARNING, "SMP: No response found");
        return;
    }
    
    for (uint64_t i = 0; i < smp_response->cpu_count; i++) {
        if (i >= MAX_CPUS)
            break;
        struct limine_smp_info* cpu_info = smp_response->cpus[i];
        
        // Skip the BSP
        if (cpu_info->lapic_id != smp_response->bsp_lapic_id) {
            // Initialize this AP's cpu_t
            cpus[i].lapic_id = (int)cpu_info->lapic_id;
            cpus[i].cpu_index = (int)i;
            cpus[i].self = &cpus[i];
            cpus[i].active_thread = nullptr;
            
            // Pass cpu_t pointer to AP
            cpu_info->extra_argument = (uint64_t)&cpus[i];
            
            // Tell Limine to start this AP at ap_main
            cpu_info->goto_address = ap_main;
        }
    }
    
    // Wait for APs to start
    for (volatile int i = 0; i < 10000000; i++);
    
    boot_message(INFO, "SMP: Started %d/%ld CPUs", 
                 __atomic_load_n(&cpus_started, __ATOMIC_SEQ_CST),
                 smp_response->cpu_count);
}
```

---

### 3. AP Entry Point (`ap_main`)

Each AP starts executing here:

```c
[[noreturn]]
static void ap_main(struct limine_smp_info* info)
{
    // Enable SSE/AVX
    enable_simd();
    
    // Get our cpu_t pointer from bootloader
    cpu_t* cpu = (cpu_t*)info->extra_argument;
    
    // Set up GS segment
    wrmsr(MSR_GS_BASE, (uint64_t)cpu);
    wrmsr(MSR_KERNEL_GS_BASE, (uint64_t)cpu);
    
    // Initialize per-CPU structures
    gdt_init();          // Load GDT and TSS
    idt_reload();        // Load IDT (shared, but IDTR must be loaded)
    apic_local_init();   // Enable this CPU's LAPIC
    syscall_init();      // Set up syscall MSRs
    
    // Signal we're ready
    __atomic_fetch_add(&cpus_started, 1, __ATOMIC_SEQ_CST);

    // Wait for BSP to initialize the scheduler, then enter it.
    while (!__atomic_load_n(&ap_scheduler_ready, __ATOMIC_SEQ_CST)) {
        __asm__ volatile("pause");
    }

    smp_init_ap_scheduler();
    __builtin_unreachable();
}
```

---

## Limine SMP Protocol

The Limine bootloader provides SMP support via a request/response protocol:

### Request (in kernel)
```c
static volatile struct limine_smp_request smp_request = {
    .id = LIMINE_SMP_REQUEST,
    .revision = 0
};
```

### Response (from bootloader)
```c
struct limine_smp_response {
    uint64_t revision;
    uint32_t flags;
    uint32_t bsp_lapic_id;      // BSP's LAPIC ID
    uint64_t cpu_count;         // Number of CPUs
    struct limine_smp_info** cpus;  // Array of CPU info
};

struct limine_smp_info {
    uint32_t processor_id;
    uint32_t lapic_id;
    uint64_t reserved;
    void (*goto_address)(struct limine_smp_info*);  // Set to start AP
    uint64_t extra_argument;    // Passed to goto_address
};
```

**How AP startup works:**
1. Kernel sets `cpu_info->goto_address = ap_main`
2. Limine sees this and starts the AP
3. AP begins executing at `ap_main(cpu_info)`

---

## Atomic Counter

The `cpus_started` counter tracks how many CPUs are online:

```c
static volatile int cpus_started = 0;

// Atomically increment when a CPU starts
__atomic_fetch_add(&cpus_started, 1, __ATOMIC_SEQ_CST);

// Atomically read the count
int count = __atomic_load_n(&cpus_started, __ATOMIC_SEQ_CST);
```

GCC/Clang atomic builtins are used instead of C11 `<stdatomic.h>` for compatibility.

---

## Startup Timeline

```
Boot
  > BSP runs kernel entry
      > smp_init_cpu0()  ─► cpus_started = 1
      > ... kernel init ...
      > smp_boot_aps()
          > Set goto_address for each AP
          > Wait loop
      > ... memory + device init ...
      > process_init()  ─► ap_scheduler_ready = true
          
  > AP1 starts at ap_main() ─► cpus_started = 2
      > wait for ap_scheduler_ready
      > smp_init_ap_scheduler() -> scheduler loop
  
  > AP2 starts at ap_main() ─► cpus_started = 3
      > wait for ap_scheduler_ready
      > smp_init_ap_scheduler() -> scheduler loop
      
  > ... more APs ...
```

---

## What Each AP Must Initialize

| Component           | Why                                  |
|---------------------|--------------------------------------|
| `enable_simd()`     | CPU-local CR0/CR4/XCR0 registers     |
| GS MSRs             | CPU-local per-CPU data pointer       |
| `gdt_init()`        | CPU-local GDT/TSS for this core      |
| `idt_reload()`      | Load shared IDT into this CPU's IDTR |
| `apic_local_init()` | Enable this CPU's Local APIC         |
| `syscall_init()`    | CPU-local STAR/LSTAR/SFMASK MSRs     |

The IDT itself is shared (same interrupt handlers for all CPUs), but each CPU must load the IDTR register.


---

## SMP Implementation Details

### Per-CPU Scheduler Threads

Each CPU owns a scheduler pseudo-thread stored in `cpu->scheduler_thread`. These threads:

- Run the `scheduler_loop()` on a dedicated kernel stack
- Are **not** part of any process thread list
- Act as the context that `schedule()` switches back to before picking work

APs enter the scheduler by waiting for `ap_scheduler_ready` and then calling
`smp_init_ap_scheduler()`, which switches onto the scheduler thread stack.

### Per-CPU Idle Threads

Each CPU has its own idle thread stored in `idle_threads[cpu_index]`. Unlike regular threads, idle threads are **not added to the process thread list**. They serve as a safe fallback (for example, when the active thread is destroyed), while the scheduler loop itself idles with `hlt` if no runnable threads exist.

```c
// Idle threads are created separately and not added to any list
static thread_t *create_idle_thread(void) {
    thread_t *thread = kmalloc(sizeof(thread_t));
    // ... initialize ...
    thread->is_idle = true;
    INIT_LIST_HEAD(&thread->list);  // Self-referential, not in any list
    return thread;
}
```

### Thread Scheduling

Timer interrupts call `scheduler_tick()`, which wakes sleepers and decrements the
current thread's time slice. If rescheduling is needed, the timer ISR invokes
`schedule()` to switch into the per-CPU scheduler thread.

The scheduler loop (see `docs/scheduler.md`) then selects a runnable thread:

- Round-robin across the global process list to avoid starvation
- Skip threads already active on another CPU
- Allow user-mode threads on any CPU

If no runnable thread is found, the scheduler switches to the per-CPU idle thread:

```c
const bool allow_user = true;
thread_t *next = find_any_runnable_thread_rr(cpu, allow_user);
if (!next) {
    next = idle_threads[cpu->cpu_index];
}
switch_to(schedt, next);
```

### IPI (Inter-Processor Interrupt) Support

The kernel supports sending IPIs between CPUs:

```c
// Send IPI to specific CPU
void apic_send_ipi(uint8_t lapic_id, uint8_t vector);

// Send IPI to all CPUs except self
void apic_send_ipi_all_excluding_self(uint8_t vector);
```

### Reschedule IPI (Vector 0xFE)

A reschedule IPI handler is registered to wake idle CPUs:

```c
static void reschedule_ipi_handler(struct interrupt_frame* frame) {
    apic_send_eoi();
    schedule();  // Check for runnable threads
}
```

This IPI can be used as a *hint* to prompt another CPU to reschedule sooner, but
normal progress does not depend on it (each CPU's LAPIC timer still drives
preemption).

### AP Initialization

After the BSP initializes the scheduler, APs wait for the `ap_scheduler_ready` flag, then:

1. Call `smp_init_ap_scheduler()`, which switches onto the per-CPU scheduler thread stack
2. `scheduler_loop()` takes over; it enables interrupts only when idling
