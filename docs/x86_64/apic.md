# What is APIC?

**APIC** stands for **Advanced Programmable Interrupt Controller**. It's the modern interrupt handling system used in x86/x86_64 PCs, replacing the older 8259 PIC (Programmable Interrupt Controller).

## Why APIC over PIC?

| Feature           | PIC (8259) | APIC             |
|-------------------|------------|------------------|
| CPUs supported    | 1          | Multiple (SMP)   |
| IRQ lines         | 15         | 24+              |
| Interrupt routing | Fixed      | Flexible per-CPU |
| Performance       | Slower     | Faster           |

APIC is essential for **multi-core systems (SMP)** because it can route interrupts to specific CPUs.

## APIC Has Two Parts

### 1. **Local APIC (LAPIC)** - One per CPU core
- Handles interrupts for that specific CPU
- Has a built-in timer (used for scheduling)
- Sends End-of-Interrupt (EOI) signals
- Each LAPIC has a unique ID

### 2. **I/O APIC (IOAPIC)** - Usually one per system
- Connects to external devices (keyboard, mouse, disks, etc.)
- Routes device interrupts to the appropriate CPU's LAPIC
- Has a redirection table that maps IRQs -> CPU + vector

---

# What `apic.c` Does

## Key Data Structures

```c
static uint64_t lapic_base = 0;      // Memory-mapped address of Local APIC
static uint64_t ioapic_base = 0;     // Memory-mapped address of I/O APIC
static uint32_t lapic_timer_ticks;   // Calibrated timer value
static struct madt_iso isos[MAX_ISOS]; // Interrupt Source Overrides (MAX_ISOS = 16)
```

## Main Functions

### `apic_init()` — Initialize the APIC system
1. **Disables the legacy PIC** (`pic_disable()`)
2. **Parses the MADT (ACPI table)** to find: (see [`acpi.md`](acpi.md))
    - LAPIC base address
    - IOAPIC base address
    - Interrupt Source Overrides (ISOs) — remappings like "IRQ 0 -> GSI 2"
3. **Calibrates the LAPIC timer** (for scheduler ticks)
4. **Configures keyboard interrupt** (IRQ 1 -> Vector 33)

### `apic_timer_calibrate()` — Calibrate the LAPIC timer
- Uses the PIT (a known-frequency timer) as reference
- Measures how many LAPIC ticks occur in 10ms
- Calculates ticks needed for the desired scheduler frequency

### `apic_local_init()` — Initialize a CPU's Local APIC
- Called once per CPU core (including on AP startup in SMP)
- Enables the LAPIC via the Spurious Vector Register
- Sets up the periodic timer (vector 32) for preemptive scheduling

### `apic_enable_irq(irq, vector)` — Route an IRQ to a vector
- Looks up any IRQ->GSI remapping
- Configures the IOAPIC redirection table entry
- Handles polarity (active high/low) and trigger mode (edge/level)
- Currently hardcodes destination to LAPIC ID 0

**Note on IOAPIC destination:** `apic_init()` routes the keyboard interrupt to the *current BSP's LAPIC ID* (read dynamically via `apic_lapic_read(LAPIC_ID) >> 24`), which is correct. However, `apic_enable_irq()` hardcodes the destination to LAPIC ID 0 (`(uint64_t)0 << 56`). This works when the BSP happens to have LAPIC ID 0 but would be incorrect on systems where the BSP has a different ID.

### `apic_send_ipi()` / `apic_send_ipi_all_excluding_self()`
- Sends a fixed IPI to a specific LAPIC ID or to all CPUs except the caller
- Used for reschedule nudges and cross-CPU coordination

### `apic_send_eoi()` — Signal End-of-Interrupt
- Must be called at the end of every interrupt handler
- Tells the LAPIC "I'm done handling this interrupt"

### `ioapic_set_entry()` — Write to IOAPIC redirection table
- Maps a GSI (Global System Interrupt) to a vector + destination CPU

---

## Interrupt Flow with APIC

```
Device (keyboard) 
    IRQ 1
I/O APIC (looks up redirection table)
    Vector 33 -> CPU 0
Local APIC (CPU 0)
CPU executes interrupt handler for vector 33
Handler calls apic_send_eoi()
```

---

## Key Concepts in the Code

### GSI (Global System Interrupt)
- A system-wide interrupt number
- IRQs may be remapped to different GSIs (via ISO entries in MADT)
- Example: IRQ 0 (PIT timer) might be remapped to GSI 2

### Interrupt Source Override (ISO)
- Tells the OS "IRQ X is actually connected to GSI Y"
- Also specifies polarity and trigger mode

### LAPIC Timer
- Built into each CPU
- Used for preemptive multitasking (scheduler ticks)
- Must be calibrated since frequency varies by system
