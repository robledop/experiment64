# Interrupt Descriptor Table (`idt.c`)

The IDT (Interrupt Descriptor Table) tells the CPU where to jump when an interrupt or exception occurs. Each entry maps a vector number (0-255) to a handler function.

---

## Interrupt Categories

| Vector Range | Type          | Description                                    |
|--------------|---------------|------------------------------------------------|
| 0-31         | Exceptions    | CPU-generated (divide error, page fault, etc.) |
| 32-47        | Hardware IRQs | Device interrupts (remapped from 0-15)         |
| 48-255       | Software/User | Available for software use                     |

### Common Exceptions (0-31)

| Vector | Name                     | Has Error Code |
|--------|--------------------------|----------------|
| 0      | Divide Error             | No             |
| 6      | Invalid Opcode           | No             |
| 8      | Double Fault             | Yes            |
| 13     | General Protection Fault | Yes            |
| 14     | Page Fault               | Yes            |

### Hardware IRQs (Remapped to 32+)

| IRQ | Vector | Device           |
|-----|--------|------------------|
| 0   | 32     | Timer (PIT/APIC) |
| 1   | 33     | Keyboard         |
| 12  | 44     | Mouse            |
| 14  | 46     | IDE Primary      |
| 15  | 47     | IDE Secondary    |

---

## IDT Entry Structure

```c
struct idt_entry {
    uint16_t offset_low;   // Handler address bits 0-15
    uint16_t selector;     // Code segment selector (0x08 = kernel code)
    uint8_t  ist;          // Interrupt Stack Table index (0 = none)
    uint8_t  type_attr;    // Gate type + DPL + Present
    uint16_t offset_mid;   // Handler address bits 16-31
    uint32_t offset_high;  // Handler address bits 32-63
    uint32_t zero;         // Reserved
} __attribute__((packed));
```

Total: 16 bytes per entry, 256 entries = 4KB for the entire IDT.

---

## Gate Type Flags

```c
#define IDT_FLAG_PRESENT   0x80  // Entry is valid
#define IDT_FLAG_RING0     0x00  // Callable from Ring 0 only
#define IDT_FLAG_RING3     0x60  // Callable from Ring 3 (user mode)
#define IDT_FLAG_INTGATE   0x0E  // Interrupt Gate (clears IF on entry)
#define IDT_FLAG_TRAPGATE  0x0F  // Trap Gate (leaves IF unchanged)
```

### Interrupt Gate vs Trap Gate

| Type           | IF Flag                       | Use Case                     |
|----------------|-------------------------------|------------------------------|
| Interrupt Gate | Cleared (interrupts disabled) | Hardware IRQs, most handlers |
| Trap Gate      | Unchanged                     | Syscalls, breakpoints        |

This kernel uses Interrupt Gates for everything because syscalls go through the `syscall` instruction, not `int 0x80`.

---

## Interrupt Frame

When an interrupt occurs, the CPU pushes state onto the stack. The assembly stub pushes additional registers, creating this frame:

```c
struct interrupt_frame {
    // Pushed by isr_common_stub
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    
    // Pushed by ISR stub
    uint64_t int_no;     // Interrupt vector number
    uint64_t err_code;   // Error code (or 0 if none)
    
    // Pushed by CPU automatically
    uint64_t rip;        // Return address
    uint64_t cs;         // Code segment
    uint64_t rflags;     // Flags register
    uint64_t rsp;        // Stack pointer (if privilege change)
    uint64_t ss;         // Stack segment (if privilege change)
};
```

---

## Assembly Stubs (`interrupts.S`)

Each vector has a small stub that:

1. Pushes a dummy error code (if CPU didn't push one)
2. Pushes the vector number
3. Jumps to the common handler

```asm
; For exceptions without error codes
isr6:
    cli
    push 0          ; Dummy error code
    push 6          ; Vector number
    jmp isr_common_stub

; For exceptions with error codes (CPU already pushed it)
isr14:
    cli
    push 14         ; Vector number
    jmp isr_common_stub
```

### Common Stub Flow

```asm
isr_common_stub:
    ; Swap GS if coming from user mode
    test byte ptr [rsp + 24], 3   ; Check CPL in saved CS
    jz 1f
    swapgs                         ; Switch to kernel GS
1:
    ; Save all general-purpose registers
    push rax, rbx, rcx, ... r15
    
    ; Call C handler with frame pointer
    mov rdi, rsp
    call interrupt_handler
    
    ; Restore registers
    pop r15, r14, ... rax
    
    ; Swap GS back if returning to user mode
    test byte ptr [rsp + 24], 3
    jz 1f
    swapgs
1:
    add rsp, 16                    ; Remove int_no and err_code
    iretq                          ; Return from interrupt
```

---

## Functions

### `idt_init()` — Initialize the IDT

**Steps:**

1. **Set up IDT pointer:**
   ```c
   idtr.limit = sizeof(idt) - 1;  // 256 * 16 - 1 = 4095
   idtr.base = (uint64_t)&idt;
   ```

2. **Install all 256 entries:**
   ```c
   for (int i = 0; i < 256; i++) {
       idt_set_gate(i, isr_stub_table[i], 0x08,
                    IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_INTGATE);
       isr_handlers[i] = nullptr;
   }
   ```

3. **Register device handlers:**
   ```c
   register_interrupt_handler(32, timer_isr);      // IRQ 0
   register_interrupt_handler(33, keyboard_isr);  // IRQ 1
   register_interrupt_handler(46, ide_primary_isr);
   register_interrupt_handler(47, ide_secondary_isr);
   ```

4. **Load IDT and enable interrupts:**
   ```c
   idt_reload();
   asm volatile("sti");
   ```

---

### `idt_set_gate()` — Set an IDT Entry

```c
void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags) {
    idt[num].offset_low  = base & 0xFFFF;
    idt[num].offset_mid  = (base >> 16) & 0xFFFF;
    idt[num].offset_high = (base >> 32) & 0xFFFFFFFF;
    idt[num].selector    = sel;    // 0x08 = kernel code
    idt[num].ist         = 0;      // No IST
    idt[num].type_attr   = flags;
    idt[num].zero        = 0;
}
```

---

### `register_interrupt_handler()` — Register a Handler

Simply stores the handler pointer:

```c
void register_interrupt_handler(uint8_t vector, isr_handler_t handler) {
    isr_handlers[vector] = handler;
}
```

---

### `register_trap_handler()` — Register a Ring 3 Trap

Used for handlers that can be invoked from user mode (like breakpoints):

```c
void register_trap_handler(uint8_t vector, isr_handler_t handler) {
    isr_handlers[vector] = handler;
    // Update gate to allow Ring 3 access and use trap gate type
    idt_set_gate(vector, isr_stub_table[vector], 0x08,
                 IDT_FLAG_PRESENT | IDT_FLAG_RING3 | IDT_FLAG_TRAPGATE);
}
```

---

### `interrupt_handler()` — Main C Handler

Called by the assembly stub with a pointer to the interrupt frame:

```c
void interrupt_handler(struct interrupt_frame* frame) {
    if (isr_handlers[frame->int_no]) {
        // Call registered handler
        isr_handlers[frame->int_no](frame);
    } else if (frame->int_no < 32) {
        // Unhandled exception = panic
        printk("EXCEPTION %d at RIP 0x%lx\n", frame->int_no, frame->rip);
        
        if (frame->int_no == 14) {
            // Page fault: print faulting address
            uint64_t cr2;
            asm volatile("mov %0, cr2" : "=r"(cr2));
            printk("CR2: 0x%lx\n", cr2);
        }
        
        stack_trace();
        panic("Unhandled exception");
    }
    // Vectors 32+ without handlers are silently ignored
}
```

---

### `idt_reload()` — Load IDT Register

Called during init and on each AP (Application Processor) startup:

```c
void idt_reload(void) {
    asm volatile("lidt %0" : : "m"(idtr));
}
```

The IDT itself is shared across all CPUs, but each CPU must load the IDTR.

---

## Device ISR Handlers

### Timer ISR (Vector 32)

```c
static void timer_isr(struct interrupt_frame* frame) {
    bool need_resched = scheduler_tick();  // Update scheduler state
    apic_send_eoi();                       // Acknowledge interrupt
    if (need_resched)
        schedule();                        // Context switch if needed
}
```

**Note:** `schedule()` is called *after* EOI to avoid timer starvation.

### Keyboard ISR (Vector 33)

```c
static void keyboard_isr(struct interrupt_frame* frame) {
    keyboard_handler_main();  // Read scancode, update buffer
    apic_send_eoi();
}
```

### IDE ISRs (Vectors 46-47)

```c
static void ide_primary_isr(struct interrupt_frame* frame) {
    ide_irq_handler(0);  // Handle channel 0
    apic_send_eoi();
}
```

---

## Interrupt Flow Summary

```
1. Device raises IRQ
       ↓
2. IOAPIC routes to LAPIC (vector 33)
       ↓
3. CPU looks up IDT[33]
       ↓
4. CPU pushes state, jumps to isr33
       ↓
5. isr33 pushes vector, jumps to isr_common_stub
       ↓
6. Stub saves registers, calls interrupt_handler()
       ↓
7. interrupt_handler() calls keyboard_isr()
       ↓
8. keyboard_isr() reads key, calls apic_send_eoi()
       ↓
9. Stub restores registers, executes iretq
       ↓
10. CPU resumes interrupted code
```

---

## SWAPGS and User Mode

When an interrupt occurs while in user mode, the kernel needs access to per-CPU data via `GS`. But `GS` currently holds the user's value. The `swapgs` instruction atomically swaps `GS.base` with `MSR_KERNEL_GS_BASE`.

```asm
; On interrupt entry from user mode
test byte ptr [rsp + 24], 3   ; Check RPL in saved CS
jz 1f                          ; Skip if already Ring 0
swapgs                         ; Swap user GS ↔ kernel GS
1:
```

This is reversed on `iretq` to restore the user's `GS`.

