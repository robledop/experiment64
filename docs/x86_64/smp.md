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
    struct Thread *active_thread; // Currently running thread
    int lapic_id;               // This CPU's LAPIC ID
    struct gdt_desc gdt[7];     // Per-CPU GDT
    struct tss_entry tss;       // Per-CPU TSS
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
    // Get SMP info from bootloader
    struct limine_smp_response* smp_response = boot_get_smp_response();
    
    // Find the BSP in the CPU list
    for (uint64_t i = 0; i < smp_response->cpu_count; i++) {
        struct limine_smp_info* cpu_info = smp_response->cpus[i];
        
        if (cpu_info->lapic_id == smp_response->bsp_lapic_id) {
            // Initialize BSP's cpu_t structure
            cpus[i].lapic_id = cpu_info->lapic_id;
            cpus[i].self = &cpus[i];
            cpus[i].active_thread = nullptr;
            
            // Clear segment registers before setting MSR
            __asm__ volatile("xor eax, eax; mov gs, eax; mov fs, eax");
            
            // Set up GS for per-CPU access
            wrmsr(MSR_GS_BASE, (uint64_t)&cpus[i]);
            wrmsr(MSR_KERNEL_GS_BASE, (uint64_t)&cpus[i]);
            break;
        }
    }
    
    __atomic_fetch_add(&cpus_started, 1, __ATOMIC_SEQ_CST);
}
```

**Why clear GS/FS first?**  
In 64-bit mode, loading a null selector into `GS` ensures the CPU uses `MSR_GS_BASE` instead of the descriptor table. Without this, the MSR write might be ignored.

---

### 2. AP Startup (`smp_boot_aps`)

Called later to bring up the other CPUs:

```c
void smp_boot_aps(void)
{
    struct limine_smp_response* smp_response = boot_get_smp_response();
    
    for (uint64_t i = 0; i < smp_response->cpu_count; i++) {
        struct limine_smp_info* cpu_info = smp_response->cpus[i];
        
        // Skip the BSP
        if (cpu_info->lapic_id != smp_response->bsp_lapic_id) {
            // Initialize this AP's cpu_t
            cpus[i].lapic_id = cpu_info->lapic_id;
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
    
    // Enable interrupts and wait for work
    __asm__ volatile("sti");
    
    while (1) {
        __asm__ volatile("hlt");  // Sleep until interrupt
    }
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
  │
  ├─► BSP runs kernel entry
  │     │
  │     ├─► smp_init_cpu0()  ─► cpus_started = 1
  │     │
  │     ├─► ... kernel init ...
  │     │
  │     └─► smp_boot_aps()
  │           │
  │           ├─► Set goto_address for each AP
  │           │
  │           └─► Wait loop
  │
  ├─► AP1 starts at ap_main() ─► cpus_started = 2
  │     └─► hlt loop
  │
  ├─► AP2 starts at ap_main() ─► cpus_started = 3
  │     └─► hlt loop
  │
  └─► ... more APs ...
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

## Current Limitations

1. **Global run queue** — All CPUs share the same thread list. There's no CPU affinity, load balancing, or work-stealing. The first CPU to find a ready thread will run it.

2. **No CPU hotplug** — CPUs must be present at boot.

3. **MAX_CPUS = 32** — Hard limit on supported CPUs.

4. **Simple wait** — Uses a spin loop delay instead of proper synchronization barrier.

---

## SMP Implementation Details

### Per-CPU Idle Threads

Each CPU has its own idle thread stored in `idle_threads[cpu_index]`. Unlike regular threads, idle threads are **not added to the process thread list** - they're only used as a fallback when no other thread is ready.

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

When an AP receives a timer interrupt:

1. `timer_isr()` calls `scheduler_tick()` which wakes up blocked threads
2. If rescheduling is needed, `schedule()` is called
3. `sched()` searches all processes for a ready thread
4. If the current thread is idle, it searches from the beginning of the process list
5. If no thread is ready, the CPU switches to its idle thread

```c
if (curr->is_idle) {
    // Search all processes from the beginning
    list_for_each_entry(p, &process_list, list) {
        list_for_each_entry(t, &p->threads, list) {
            if (t->state == THREAD_READY && !t->is_idle) {
                next_thread = t;
                goto found;
            }
        }
    }
}
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

### AP Initialization

After the BSP initializes the scheduler, APs wait for the `ap_scheduler_ready` flag, then:

1. Call `smp_init_ap_scheduler()` to set their idle thread as active
2. Enable interrupts
3. Enter the idle loop (timer interrupts will trigger scheduling)

