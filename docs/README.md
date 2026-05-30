# Documentation index

Design notes and reading guides for the kernel and userland. New here? Start
with the **walkthroughs** below — they trace one real flow end to end across
files — and keep [glossary.md](glossary.md) open for the acronyms.

## Start here

- [glossary.md](glossary.md) — every acronym and bit of jargon used in the tree
- [walkthroughs/](walkthroughs/) — end-to-end traces of real flows:
  - [01 — Boot to first userland process](walkthroughs/01-boot-to-userland.md)
  - [02 — A timer tick to a context switch](walkthroughs/02-timer-tick-to-context-switch.md)
  - [03 — A syscall round-trip](walkthroughs/03-syscall-roundtrip.md)
  - [04 — open() to the first bytes off disk](walkthroughs/04-open-to-disk-bytes.md)
  - [05 — An anonymous mmap to zeroed pages](walkthroughs/05-mmap-to-pages.md)
  - [06 — Life of a TCP connection](walkthroughs/06-tcp-connection-life.md)
  - [07 — From PCI probe to a USB disk sector](walkthroughs/07-pci-to-usb-disk.md)
  - [08 — A WM client presents a frame](walkthroughs/08-wm-present-frame.md)
  - [09 — Installing, raising, and delivering a signal](walkthroughs/09-signal-delivery.md)

The README's "Reading map" section lists the matching source directories.

## CPU, interrupts, SMP

The richest beginner-facing prose in the tree. Pairs with `kernel/arch/x86_64/`.

- [x86_64/cpu.md](x86_64/cpu.md) — per-CPU state, GS base, control registers
- [x86_64/gdt.md](x86_64/gdt.md) — segment descriptors and the TSS
- [x86_64/idt.md](x86_64/idt.md) — interrupt/exception gates and ISRs
- [x86_64/apic.md](x86_64/apic.md) — LAPIC/IOAPIC interrupt routing
- [x86_64/smp.md](x86_64/smp.md) — multi-CPU bring-up
- [x86_64/acpi.md](x86_64/acpi.md) — firmware tables (MADT, etc.)

## Memory

- [address_space.md](address_space.md) — the full virtual address-space layout
- [pmm.md](pmm.md) — physical memory manager (bitmap allocator)
- [vmm.md](vmm.md) — virtual memory manager (4-level page tables)
- [heap.md](heap.md) — kernel heap (slab + big allocations)
- [dma.md](dma.md) — contiguous DMA allocator

## Tasks, scheduling, IPC

- [scheduler.md](scheduler.md) — the xv6-style per-CPU scheduler
- [signals.md](signals.md) — signal delivery and the return trampoline
- [pthreads.md](pthreads.md) — the userland pthread subset
- [tls.md](tls.md) — thread-local storage via FSGSBASE
- [futex.md](futex.md) — kernel-assisted wait/wake
- [shm.md](shm.md) — named shared memory

## Storage and filesystems

- [storage.md](storage.md) — block devices, GPT scan, boot-disk detection
- [ext2.md](ext2.md) — ext2 permission defaults (driver design is in the code; see walkthrough 04)
- [dynamic_linking.md](dynamic_linking.md) — `/lib/ld.so` and shared libraries

## Syscalls and I/O

- [syscalls.md](syscalls.md) — the syscall layer and error conventions
- [ioctl.md](ioctl.md) — supported `ioctl` requests

## Networking

- [networking.md](networking.md) — Ethernet/IPv4/UDP/TCP, ARP, ICMP, DHCP, sockets

## Graphics and userland

- [wm_protocol.md](wm_protocol.md) — the window-manager IPC protocol
- [shell.md](shell.md) — the shell
- [doom.md](doom.md) — the DOOM port

## Reviews and project notes

- [posix_compliance_review.md](posix_compliance_review.md) — POSIX gap analysis
- [simplification_tracker.md](simplification_tracker.md) — the structural-cleanup backlog
