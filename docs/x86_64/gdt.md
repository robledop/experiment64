# Global Descriptor Table (`gdt.c`)

The GDT (Global Descriptor Table) is a fundamental x86 data structure that defines memory segments and privilege levels. In 64-bit long mode, segmentation is mostly disabled, but the GDT is still required for:

- **Privilege levels** (Ring 0 vs Ring 3)
- **Task State Segment (TSS)** for stack switching on interrupts
- **Syscall/Sysret** segment selectors

---

## Segment Selectors

A segment selector is a 16-bit value loaded into segment registers (`CS`, `DS`, `SS`, etc.):

```
Bits 15-3: Index into GDT
Bits 2:    TI (0 = GDT, 1 = LDT)
Bits 1-0:  RPL (Requested Privilege Level)
```

---

## GDT Layout

| Index | Selector | Description                     |
|-------|----------|---------------------------------|
| 0     | `0x00`   | Null descriptor (required)      |
| 1     | `0x08`   | Kernel Code (Ring 0, 64-bit)    |
| 2     | `0x10`   | Kernel Data (Ring 0)            |
| 3     | `0x18`   | User Data (Ring 3)              |
| 4     | `0x20`   | User Code (Ring 3, 64-bit)      |
| 5-6   | `0x28`   | TSS (16-byte system descriptor) |

**Note:** User Data comes before User Code. This ordering is required by the `syscall`/`sysret` instructions which expect a specific layout.

---

## Access Byte Flags

```c
#define GDT_ACCESS_PRESENT 0x80  // Segment is valid
#define GDT_ACCESS_RING0   0x00  // Privilege level 0 (kernel)
#define GDT_ACCESS_RING3   0x60  // Privilege level 3 (user)
#define GDT_ACCESS_S       0x10  // 1 = code/data, 0 = system
#define GDT_ACCESS_EXEC    0x08  // Executable (code segment)
#define GDT_ACCESS_DC      0x04  // Direction/Conforming
#define GDT_ACCESS_RW      0x02  // Readable (code) / Writable (data)
#define GDT_ACCESS_AC      0x01  // Accessed bit
#define GDT_ACCESS_TSS     0x09  // Available 64-bit TSS type
```

### Access Byte Layout
```
Bit 7:   Present (P)
Bits 6-5: DPL (Descriptor Privilege Level)
Bit 4:   S (Descriptor type: 0=system, 1=code/data)
Bits 3-0: Type
```

---

## Granularity/Flags Byte

```c
#define GDT_FLAG_GRAN  0x80  // Limit in 4KB pages (not used in 64-bit)
#define GDT_FLAG_SIZE  0x40  // 32-bit segment
#define GDT_FLAG_LONG  0x20  // 64-bit code segment
```

In 64-bit mode, only the `LONG` flag matters for code segments.

---

## Descriptor Structures

### Standard Descriptor (8 bytes)
```c
struct gdt_desc {
    uint16_t limit;      // Limit bits 0-15
    uint16_t base_low;   // Base bits 0-15
    uint8_t  base_mid;   // Base bits 16-23
    uint8_t  access;     // Access byte
    uint8_t  granularity;// Flags + Limit bits 16-19
    uint8_t  base_high;  // Base bits 24-31
};
```

### System Descriptor (16 bytes) — Used for TSS
```c
struct gdt_system_desc {
    uint16_t limit;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
    uint32_t base_upper;  // Base bits 32-63 (64-bit extension)
    uint32_t reserved;
};
```

---

## Task State Segment (TSS)

The TSS is a hardware structure used for:

1. **Stack switching on privilege change** — When an interrupt occurs in user mode, the CPU loads `RSP0` from the TSS as the kernel stack pointer.

2. **IST (Interrupt Stack Table)** — Up to 7 dedicated stacks for specific interrupts (e.g., double fault, NMI).

### TSS Structure
```c
struct tss_entry {
    uint32_t reserved0;
    uint64_t rsp0;       // Stack for Ring 0 (kernel)
    uint64_t rsp1;       // Stack for Ring 1 (unused)
    uint64_t rsp2;       // Stack for Ring 2 (unused)
    uint64_t reserved1;
    uint64_t ist1;       // Interrupt Stack Table entries
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base; // Offset to I/O permission bitmap
};
```

---

## Functions

### `gdt_init()` — Initialize GDT and TSS

Called once per CPU core (each core needs its own GDT/TSS).

**Steps:**

1. **Get per-CPU structures:**
   ```c
   cpu_t* cpu = get_cpu();
   struct gdt_desc* gdt = cpu->gdt;
   struct tss_entry* tss = &cpu->tss;
   ```

2. **Set up GDT pointer:**
   ```c
   gdtp.limit = sizeof(struct gdt_desc) * 7 - 1;
   gdtp.base = (uint64_t)gdt;
   ```

3. **Create descriptors:**
   - Null descriptor (index 0)
   - Kernel code/data (indices 1-2)
   - User data/code (indices 3-4)
   - TSS system descriptor (indices 5-6, 16 bytes)

4. **Initialize TSS:**
   ```c
   memset(tss, 0, sizeof(struct tss_entry));
   tss->iomap_base = sizeof(struct tss_entry); // Disable I/O bitmap
   ```

5. **Load GDT:**
   ```asm
   lgdt [gdtp]
   ```

6. **Reload segment registers:**
   ```asm
   ; Far return to reload CS
   push 0x08
   lea rax, [rip + 1f]
   push rax
   retfq
   1:
   ; Reload data segments
   mov ax, 0x10
   mov ds, ax
   mov es, ax
   mov ss, ax
   xor ax, ax
   mov fs, ax
   mov gs, ax
   ```

7. **Restore GS base** (loading GS clears the base):
   ```c
   wrmsr(MSR_GS_BASE, (uint64_t)cpu);
   wrmsr(MSR_KERNEL_GS_BASE, (uint64_t)cpu);
   ```

8. **Load TSS:**
   ```asm
   ltr ax  ; ax = 0x28 (TSS selector)
   ```

---

### `tss_set_stack(uint64_t stack)` — Set Kernel Stack

Called during context switch to update the kernel stack pointer.

```c
void tss_set_stack(uint64_t stack) {
    cpu_t* cpu = get_cpu();
    cpu->tss.rsp0 = stack;
}
```

When a user-mode program is interrupted (e.g., syscall, timer), the CPU loads this stack pointer automatically.

---

## Why Per-CPU GDT/TSS?

Each CPU core needs its own:
- **GDT** — Points to its own TSS
- **TSS** — Contains that core's kernel stack pointer

If all cores shared one TSS, they'd all try to use the same kernel stack = instant corruption.

---

## Syscall Segment Layout

The `syscall`/`sysret` instructions expect segments in a specific order. The
kernel programs STAR like this (see `syscall_init()`):

```
STAR[63:48] = 0x10  ; user base selector
STAR[47:32] = 0x08  ; kernel code selector
```

This yields:
- `SYSCALL` -> CS = `0x08`, SS = `0x10`
- `SYSRET`  -> CS = `0x20`, SS = `0x18` (base + 16 and base + 8)

That is why User Data (0x18) comes before User Code (0x20) in the GDT layout.

---

## Interrupt Flow with TSS

```
User mode (Ring 3, user stack)
Interrupt/Exception occurs
CPU reads TSS.rsp0
CPU switches to kernel stack (Ring 0)
CPU pushes SS, RSP, RFLAGS, CS, RIP
Jump to interrupt handler
```

This automatic stack switch is why `tss_set_stack()` must be called on every context switch — each thread needs its own kernel stack.
