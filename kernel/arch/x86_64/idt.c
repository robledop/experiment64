/**
 * @file idt.c
 * @brief Interrupt Descriptor Table setup, CPU exception handlers, and IRQ ISRs.
 *
 * Builds the IDT, installs exception/fault handlers, and registers the device
 * IRQ handlers (timer, keyboard, IDE, reschedule IPI) via register_interrupt_handler().
 *
 * Heads-up on the device-IRQ split: a few drivers keep both their IRQ-enable
 * and their ISR in one file (see mouse.c / e1000.c / atl1c.c), but the keyboard
 * and IDE ISR bodies live HERE while their IRQ lines are unmasked elsewhere.
 * keyboard_isr / ide_primary_isr / ide_secondary_isr below are thin wrappers
 * that delegate to keyboard_handler_main() (kernel/drivers/keyboard.c) and
 * ide_irq_handler() (kernel/drivers/ide.c).
 */
#include <arch/x86_64/apic.h>
#include <arch/x86_64/idt.h>
#include <debug.h>
#include <drivers/ide.h>
#include <drivers/keyboard.h>
#include <drivers/terminal.h>
#include <kernel.h>
#include <lib/util.h>
#include <limine.h>
#include <sys/signal.h>
#include <syscall_common.h>
#include <task/process.h>
#include <task/signal.h>

#define IDT_FLAG_PRESENT 0x80
#define IDT_FLAG_RING0 0x00
#define IDT_FLAG_RING3 0x60
#define IDT_FLAG_INTGATE 0x0E
#define IDT_FLAG_TRAPGATE 0x0F

#define IRQ_BASE 32
#define IRQ_KEYBOARD 1
#define IRQ_MOUSE 12
#define IRQ_IDE_PRIMARY 14
#define IRQ_IDE_SECONDARY 15

extern volatile struct limine_framebuffer_request framebuffer_request;

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

__attribute__((aligned(0x10))) static struct idt_entry idt[256];
static struct idt_ptr idtr;
static isr_handler_t isr_handlers[256];

extern void *isr_stub_table[];

char *exception_messages[] = {"Division By Zero",
                              "Debug",
                              "Non Maskable Interrupt",
                              "Breakpoint",
                              "Into Detected Overflow",
                              "Out of Bounds",
                              "Invalid Opcode",
                              "No Coprocessor",
                              "Double Fault",
                              "Coprocessor Segment Overrun",
                              "Bad TSS",
                              "Segment Not Present",
                              "Stack Fault",
                              "General Protection Fault",
                              "Page Fault",
                              "Unknown Interrupt",
                              "x87 FPU Floating-Point Error",
                              "Alignment Check",
                              "Machine Check",
                              "SIMD Floating-Point Exception",
                              "Virtualization Exception",
                              "Control Protection Exception",
                              "Reserved",
                              "Reserved",
                              "Reserved",
                              "Reserved",
                              "Reserved",
                              "Reserved",
                              "Reserved",
                              "Reserved",
                              "Security Exception",
                              "Reserved"};

// Map x86 exception vectors to POSIX signal numbers.
static constexpr int vector_to_signal[32] = {
    [0]  = SIGFPE,  // Division by Zero
    [4]  = SIGFPE,  // Overflow
    [5]  = SIGSEGV, // Bound Range Exceeded
    [6]  = SIGILL,  // Invalid Opcode
    [7]  = SIGFPE,  // No Coprocessor
    [8]  = SIGSEGV, // Double Fault
    [10] = SIGSEGV, // Bad TSS
    [11] = SIGBUS,  // Segment Not Present
    [12] = SIGSEGV, // Stack Fault
    [13] = SIGSEGV, // General Protection Fault
    [14] = SIGSEGV, // Page Fault
    [16] = SIGFPE,  // x87 FPU Error
    [17] = SIGBUS,  // Alignment Check
    [19] = SIGFPE,  // SIMD Floating-Point Exception
};

// Walk the user-mode frame pointer chain and capture return addresses.
static void capture_user_backtrace(crash_info_t *info, uint64_t rbp)
{
    info->frame_count = 0;

    struct frame {
        uint64_t rbp;
        uint64_t rip;
    };

    for (int i = 0; i < MAX_CRASH_FRAMES && rbp != 0; i++) {
        if (!user_ptr_read_ok((const void *)rbp, sizeof(struct frame), "backtrace"))
            break;
        struct frame f;
        if (!copy_from_user(&f, (const void *)rbp, sizeof(f)))
            break;
        if (f.rip == 0)
            break;
        info->frames[info->frame_count++] = f.rip;
        rbp                               = f.rbp;
    }
}

void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags)
{
    idt[num].offset_low  = base & 0xFFFF;
    idt[num].offset_mid  = (base >> 16) & 0xFFFF;
    idt[num].offset_high = (base >> 32) & 0xFFFFFFFF;
    idt[num].selector    = sel;
    idt[num].ist         = 0;
    idt[num].type_attr   = flags;
    idt[num].zero        = 0;
}

void register_interrupt_handler(uint8_t vector, isr_handler_t handler)
{
    isr_handlers[vector] = handler;
}

void register_trap_handler(uint8_t vector, isr_handler_t handler)
{
    isr_handlers[vector] = handler;
    idt_set_gate(vector, (uint64_t)isr_stub_table[vector], 0x08, IDT_FLAG_PRESENT | IDT_FLAG_RING3 | IDT_FLAG_TRAPGATE);
}

static void timer_isr([[maybe_unused]] struct interrupt_frame *frame)
{
    bool need_resched = scheduler_tick();
    apic_send_eoi();
    if (need_resched) {
        cpu_interrupt_exit();
        schedule();
        cpu_interrupt_enter();
    }
}

// The IRQ 1 line is unmasked in the IOAPIC init in kernel/arch/x86_64/apic.c,
// not in the keyboard driver; the work runs in keyboard_handler_main()
// (kernel/drivers/keyboard.c).
static void keyboard_isr([[maybe_unused]] struct interrupt_frame *frame)
{
    keyboard_handler_main();
    apic_send_eoi();
}

// IRQ 14/15 are unmasked from ide_init() via apic_enable_irq() in
// kernel/drivers/ide.c, where ide_irq_handler() is also defined.
static void ide_primary_isr([[maybe_unused]] struct interrupt_frame *frame)
{
    ide_irq_handler(0);
    apic_send_eoi();
}

static void ide_secondary_isr([[maybe_unused]] struct interrupt_frame *frame)
{
    ide_irq_handler(1);
    apic_send_eoi();
}

static void reschedule_ipi_handler([[maybe_unused]] struct interrupt_frame *frame)
{
    apic_send_eoi();
    cpu_interrupt_exit();
    schedule();
    cpu_interrupt_enter();
}

static void dump_panic_context(const struct interrupt_frame *frame, const struct interrupt_frame *snapshot)
{
    cpu_t *cpu        = get_cpu();
    uint64_t curr_rsp = 0;
    __asm__ volatile("mov %0, rsp" : "=r"(curr_rsp));
    uint64_t cr2 = 0;
    __asm__ volatile("mov %0, cr2" : "=r"(cr2));

    printk(KBWHT "frame" KRESET "=%p " KBWHT "int" KRESET "=%lu " KBWHT "err" KRESET "=0x%lx\n",
           frame,
           (unsigned long)snapshot->int_no,
           snapshot->err_code);
    printk("snapshot " KBWHT "rip" KRESET "=0x%lx " KBWHT "cs" KRESET "=0x%lx " KBWHT "rflags" KRESET "=0x%lx " KBWHT
           "rsp" KRESET "=0x%lx " KBWHT "ss" KRESET "=0x%lx " KBWHT "cr2" KRESET "=0x%lx " KBWHT "curr_rsp" KRESET
           "=0x%lx\n",
           snapshot->rip,
           snapshot->cs,
           snapshot->rflags,
           snapshot->rsp,
           snapshot->ss,
           cr2,
           curr_rsp);


    if (frame->int_no != snapshot->int_no || frame->err_code != snapshot->err_code || frame->rip != snapshot->rip ||
        frame->cs != snapshot->cs || frame->rflags != snapshot->rflags || frame->rsp != snapshot->rsp ||
        frame->ss != snapshot->ss) {
        printk(KBYEL "frame changed in handler" KBWHT " int" KRESET "=%lu " KBWHT "err" KRESET "=0x%lx " KBWHT
                     "rip" KRESET "=0x%lx " KBWHT "cs" KRESET "=0x%lx " KBWHT "rflags" KRESET "=0x%lx " KBWHT
                     "rsp" KRESET "=0x%lx " KBWHT "ss" KRESET "=0x%lx\n",
               (unsigned long)frame->int_no,
               frame->err_code,
               frame->rip,
               frame->cs,
               frame->rflags,
               frame->rsp,
               frame->ss);
    }
    if (!cpu) {
        printk("cpu=null\n");
        return;
    }

    printk(KBWHT "cpu" KRESET "=%p " KBWHT "idx" KRESET "=%d " KBWHT "kernel_rsp" KRESET "=0x%lx " KBWHT
                 "user_rsp" KRESET "=0x%lx " KBWHT "tss.rsp0" KRESET "=0x%lx " KBWHT "active_thread" KRESET "=%p\n",
           cpu,
           cpu->cpu_index,
           cpu->kernel_rsp,
           cpu->user_rsp,
           cpu->tss.rsp0,
           cpu->active_thread);

    thread_t *t            = cpu->active_thread;
    const uintptr_t t_addr = (uintptr_t)t;
    // Ensure the thread pointer is properly aligned
    if (t && (t_addr % __alignof__(thread_t)) == 0) {
        process_t *p = t->process;
        int pid      = -1;
        if (p && (((uintptr_t)p) % __alignof__(process_t)) == 0)
            pid = p->pid;

        printk(KBWHT "tid" KRESET "=%d " KBWHT "state" KRESET "=%u " KBWHT "is_user" KRESET "=%d " KBWHT
                     "is_idle" KRESET "=%d " KBWHT "kstack_top" KRESET "=0x%lx " KBWHT "rsp" KRESET "=0x%lx " KBWHT
                     "saved_user_rsp" KRESET "=0x%lx " KBWHT "pid" KRESET "=%d " KBWHT "process" KRESET "=%p\n",
               t->tid,
               (unsigned)t->state,
               t->is_user,
               t->is_idle,
               t->kstack_top,
               t->rsp,
               t->saved_user_rsp,
               pid,
               p);

        if (t->kstack_top != 0) {
            const uintptr_t kbase = t->kstack_top - KSTACK_SIZE;
            const uintptr_t ktop  = t->kstack_top;
            const uintptr_t faddr = (uintptr_t)frame;
            const bool in_kstack  = (faddr >= kbase && faddr < ktop);
            printk(
                KBWHT "kstack" KRESET "=[0x%lx-0x%lx) " KBWHT "frame_in_kstack" KRESET "=%d\n", kbase, ktop, in_kstack);
        }
    } else {
        printk(KYEL "active_thread invalid or misaligned (ptr=%p)\n" KRESET, t);
    }


    if (t && t->is_user) {
        printk(KRESET "run " KBBLU "addr2line -e user/build/%s %p" KRESET " to get line numbers\n",
               current_process->name,
               snapshot->rip);
        printk(KRESET "run " KBBLU "objdump -d user/build/%s | grep %p -A 40 -B 40" KRESET " to see more.\n",
               current_process->name,
               snapshot->rip);
    } else {
        printk(KRESET "run " KBBLU "addr2line -e build/kernel.elf %p" KRESET " to get line numbers\n", snapshot->rip);
        printk(KRESET "run " KBBLU "objdump -d build/kernel.elf | grep %p -A 40 -B 40" KRESET " to see more.\n",
               snapshot->rip);
    }

    printk(KBWHT "frame cs" KRESET "=0x%lx " KBWHT "ss" KRESET "=0x%lx " KBWHT "rsp" KRESET "=0x%lx " KBWHT
                 "rflags" KRESET "=0x%lx\n",
           snapshot->cs,
           snapshot->ss,
           snapshot->rsp,
           snapshot->rflags);
}

void interrupt_handler(struct interrupt_frame *frame)
{
    cpu_interrupt_enter();
    if (isr_handlers[frame->int_no]) {
        isr_handlers[frame->int_no](frame);
    } else if (frame->int_no < 32) {
        struct interrupt_frame snapshot    = *frame;
        const struct interrupt_frame *snap = &snapshot;

        char *message = frame->int_no >= ARRAY_SIZE(exception_messages) ? "Unknown" : exception_messages[snap->int_no];
        printk(KRED "PANIC: EXCEPTION OCCURRED! " KYEL "%s" KRESET " (Vector %d)\n", message, (int)snap->int_no);

        if (snap->int_no == 14) {
            uint64_t cr2;
            __asm__ volatile("mov %0, cr2" : "=r"(cr2));
            printk(KBWHT "CR2 (Page Fault Address):" KRESET " 0x%lx\n", cr2);
#ifdef TEST_MODE
            printk("PF DEBUG: rip=0x%lx cs=0x%lx rsp=0x%lx err=0x%lx cr2=0x%lx\n",
                   snap->rip,
                   snap->cs,
                   snap->rsp,
                   snap->err_code,
                   cr2);
            uint64_t cr3;
            __asm__ volatile("mov %0, cr3" : "=r"(cr3));
            printk("Process: pid=%d cr3=0x%lx\n", current_process ? current_process->pid : -1, cr3);
            printk("Regs: rax=0x%lx rbx=0x%lx rcx=0x%lx rdx=0x%lx\n", snap->rax, snap->rbx, snap->rcx, snap->rdx);
            printk("Regs: rsi=0x%lx rdi=0x%lx rbp=0x%lx rsp=0x%lx\n", snap->rsi, snap->rdi, snap->rbp, snap->rsp);
#endif
        }

        // If the fault came from user mode, treat it as a bad process and reap it
        // rather than panicking the whole kernel.
        const bool user_mode = (snap->cs & 0x3) != 0;

        if (user_mode) {
            thread_t *t  = current_thread;
            process_t *p = current_process;
            printk("Killing user process \"%s\" pid=%d tid=%d rip=0x%lx rsp=0x%lx err=0x%lx\n",
                   p != nullptr ? p->name : "unknown",
                   p != nullptr ? p->pid : -1,
                   t != nullptr ? t->tid : -1,
                   snap->rip,
                   snap->rsp,
                   snap->err_code);

            printk(KWHT "run " KBBLU "addr2line -e <binary> %p" KWHT " to get line numbers\n", snap->rip);
            printk(KWHT "run " KBBLU "objdump -d <binary> | grep %p -A 40 -B 40" KWHT " to see more.\n", snap->rip);

            // Capture crash info before acquiring the lock (needs to read user pages).
            if (p) {
                p->crash_info.fault_rip = snap->rip;
                capture_user_backtrace(&p->crash_info, snap->rbp);
            }

            int sig = (snap->int_no < ARRAY_SIZE(vector_to_signal) && vector_to_signal[snap->int_no])
                ? vector_to_signal[snap->int_no]
                : SIGSEGV;

            // Acquire scheduler lock before modifying thread/process state to prevent races.
            // Interrupts are already disabled by the interrupt gate.
            spinlock_acquire(&scheduler_lock);

            process_t *parent = nullptr;
            if (p)
                process_mark_exited_locked(p, 128 + sig, &parent);

            spinlock_release(&scheduler_lock);

            // Wake up the parent outside the lock (thread_wakeup acquires its own lock)
            if (parent) {
                thread_wakeup(parent);
            }

            // Close FDs eagerly so that pipe readers (e.g. the window manager)
            // see EOF immediately rather than waiting for the full process reap.
            process_close_fds(p);

            // schedule() will switch to another thread; interrupts will be re-enabled
            // when the new thread runs.
            cpu_interrupt_exit();
            schedule();
            // schedule() should not return to the faulting context, but bail out defensively.
            return;
        }

        dump_panic_context(frame, snap);
        stack_trace();

#ifdef TEST_MODE
        shutdown();
#endif
        hcf();
    } else if (frame->int_no != 0xFF) {
        // Vector 0xFF is the LAPIC spurious vector; it must not be acknowledged
        // with an EOI (no in-service bit is set for it).
        apic_send_eoi();
    }

    if (frame->int_no >= 32) {
        signal_deliver_after_interrupt(frame);
    }

    cpu_interrupt_exit();
}

void idt_init(void)
{
    idtr.limit = sizeof(idt) - 1;
    idtr.base  = (uint64_t)&idt;

    for (int i = 0; i < 256; i++) {
        // Use Interrupt Gates (0x0E) for all entries to automatically disable interrupts
        // upon entry. This avoids the need for manual 'cli' instructions in handlers.
        // If we wanted to allow nested interrupts (e.g. for system calls or non-critical exceptions),
        // we would use Trap Gates (0x0F) instead, but our syscalls use the 'syscall' instruction.
        idt_set_gate(i, (uint64_t)isr_stub_table[i], 0x08, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_INTGATE);
        isr_handlers[i] = nullptr;
    }

    register_interrupt_handler(IRQ_BASE + 0, timer_isr);
    register_interrupt_handler(IRQ_BASE + IRQ_KEYBOARD, keyboard_isr);
    register_interrupt_handler(IRQ_BASE + IRQ_IDE_PRIMARY, ide_primary_isr);
    register_interrupt_handler(IRQ_BASE + IRQ_IDE_SECONDARY, ide_secondary_isr);
    register_interrupt_handler(IPI_RESCHEDULE_VECTOR, reschedule_ipi_handler);

    idt_reload();
    __asm__ volatile("sti");
}

void idt_reload(void)
{
    __asm__ volatile("lidt %0" : : "m"(idtr));
}