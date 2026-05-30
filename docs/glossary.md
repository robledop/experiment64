# Glossary

Acronyms and jargon used across the kernel, drivers, networking, filesystems,
and userland. If you hit a term in the code or in another doc and it isn't
obvious, it's probably here. Terms are kernel-wide; where something is specific
to one subsystem the expansion says so (e.g. xHCI register names).

| Term                    | Meaning                                                                   |
|-------------------------|---------------------------------------------------------------------------|
| ABAR                    | AHCI Base Address Register (PCI BAR5)                                     |
| ACPI                    | Advanced Configuration and Power Interface firmware tables                |
| AHCI                    | Advanced Host Controller Interface for SATA                               |
| AP                      | Application Processor (a secondary CPU in SMP)                            |
| APIC                    | Advanced Programmable Interrupt Controller                                |
| ARGB                    | 32-bit pixel: alpha, red, green, blue                                     |
| ARP                     | Address Resolution Protocol; maps an IP to a MAC address                  |
| ATAPI                   | ATA Packet Interface (CD/DVD over ATA)                                    |
| auxv                    | auxiliary vector passed to a process at start                             |
| BAR                     | Base Address Register; a PCI device's MMIO/IO window                      |
| bmap                    | map a file's logical block to a disk sector                               |
| BOT                     | Bulk-Only Transport (the USB mass-storage protocol)                       |
| BPB                     | BIOS Parameter Block; FAT32 boot-sector geometry                          |
| bread / brelse / bwrite | buffer-cache read / release / write of a disk block                       |
| BSP                     | Bootstrap Processor (the first CPU)                                       |
| CBW / CSW               | Command Block Wrapper / Command Status Wrapper (BOT)                      |
| CCS                     | Current Connect Status (an xHCI port has a device)                        |
| CR0.WP                  | Write-Protect bit; faults kernel writes to read-only pages                |
| CR2                     | control register holding the faulting address after a #PF                 |
| CR3                     | control register holding the active PML4 physical address                 |
| CRCR                    | Command Ring Control Register (xHCI)                                      |
| CSI                     | Control Sequence Introducer (`ESC [ ...`) for ANSI escapes                |
| DCBAA                   | Device Context Base Address Array (xHCI)                                  |
| DCS                     | Dequeue Cycle State (xHCI ring cycle bit)                                 |
| demand paging           | lazy page allocation on fault (NOT used here; mapping is eager)           |
| devfs                   | the `/dev` virtual filesystem of device nodes                             |
| DHCP                    | Dynamic Host Configuration Protocol; auto-assigns IP/gateway/DNS over UDP |
| DIND / TIND             | doubly / triply indirect ext2 block pointers                              |
| dirent                  | directory entry; a name plus an inode number                              |
| DMA                     | Direct Memory Access (a device reads/writes RAM directly)                 |
| EFER                    | Extended Feature Enable Register (an MSR)                                 |
| EHCI                    | Enhanced Host Controller Interface (legacy USB 2.0)                       |
| EOC                     | End Of Cluster chain marker in the FAT                                    |
| EOI                     | End Of Interrupt acknowledgement to the (L)APIC                           |
| EP0                     | Endpoint 0, the USB default control pipe                                  |
| ephemeral port          | auto-assigned client port (range 49152-65535)                             |
| ERDP                    | Event Ring Dequeue Pointer (xHCI)                                         |
| ERST                    | Event Ring Segment Table (xHCI)                                           |
| ESP                     | EFI System Partition (the FAT volume holding the bootloader)              |
| ethertype               | 16-bit Ethernet field naming the L3 protocol                              |
| FAT                     | File Allocation Table; per-cluster next-cluster chain                     |
| fd                      | file descriptor; index into the per-process open-file table               |
| fsbase                  | FS-segment base register holding the thread-local-storage pointer         |
| FSGSBASE                | CPU feature: userspace read/write of the FS/GS base                       |
| futex                   | fast userspace mutex; kernel-assisted wait/wake                           |
| GDT                     | Global Descriptor Table of segment descriptors                            |
| GHC                     | Global HBA Control register (AHCI)                                        |
| GPT                     | GUID Partition Table; the disk partition layout                           |
| GSI                     | Global System Interrupt number                                            |
| HBA                     | Host Bus Adapter (the SATA controller)                                    |
| HCF                     | Halt and Catch Fire (`cli` then `hlt` forever)                            |
| HHDM                    | Higher Half Direct Map of physical RAM (the phys-to-virt offset)          |
| HID                     | Human Interface Device (USB mouse/keyboard/tablet)                        |
| icache                  | in-memory cache of ext2 inodes                                            |
| ICMP                    | Internet Control Message Protocol (e.g. ping echo)                        |
| ICR                     | Interrupt Command Register; sends IPIs                                    |
| IDT                     | Interrupt Descriptor Table of gate descriptors                            |
| iget / ilock            | get a cached inode slot; lazily load+lock it from disk                    |
| IHL                     | Internet Header Length (IPv4 header size in 32-bit words)                 |
| imui                    | immediate-mode user interface (rebuild widgets each frame)                |
| inode                   | filesystem object metadata record (size, blocks, mode)                    |
| inode_operations / iops | per-inode function-pointer vtable for read/finddir/etc.                   |
| invlpg                  | instruction invalidating one TLB entry                                    |
| IOAPIC                  | I/O APIC; routes device IRQs to CPUs                                      |
| IPC                     | Inter-Process Communication                                               |
| IPI                     | Inter-Processor Interrupt                                                 |
| IRQ                     | Interrupt Request line/number                                             |
| iretq                   | 64-bit interrupt-return instruction (can drop to ring 3)                  |
| ISN                     | Initial Sequence Number (TCP handshake start value)                       |
| ISO (MADT)              | Interrupt Source Override (ACPI); NOT a disk image                        |
| ISR                     | Interrupt Service Routine / stub                                          |
| IST                     | Interrupt Stack Table; dedicated fault stacks                             |
| LAPIC                   | Local APIC (per-CPU interrupt unit)                                       |
| LBA                     | Logical Block Address; a sector number on disk                            |
| Limine                  | the bootloader that hands off to the kernel                               |
| LSTAR                   | MSR holding the `syscall` entry RIP                                       |
| LTR                     | Load Task Register instruction (loads the TSS)                            |
| LUN                     | Logical Unit Number                                                       |
| LVT                     | Local Vector Table (LAPIC interrupt sources)                              |
| MAC address             | 6-byte Ethernet hardware address                                          |
| MADT                    | Multiple APIC Description Table (ACPI)                                    |
| magic cookie            | `0x63825363` marker before DHCP options                                   |
| MMIO                    | Memory-Mapped I/O register access                                         |
| morecore                | K&R allocator routine requesting more heap from the OS                    |
| MSC                     | Mass Storage Class (USB)                                                  |
| MSI / MSI-X             | Message Signaled Interrupts via a memory write                            |
| MSR                     | Model-Specific Register (`rdmsr`/`wrmsr`)                                 |
| MSS                     | Maximum Segment Size; largest TCP data chunk per frame                    |
| MTU                     | Maximum Transmission Unit (1500-byte payload cap)                         |
| OPOST                   | termios flag enabling output post-processing (NL->CRNL)                   |
| PCI                     | Peripheral Component Interconnect device bus                              |
| PD                      | Page Directory (level-2 page table)                                       |
| PDPT                    | Page Directory Pointer Table (level-3 table)                              |
| PIC                     | Programmable Interrupt Controller (legacy 8259)                           |
| PIT                     | Programmable Interval Timer (legacy 8254)                                 |
| PML4                    | Page Map Level 4 (the top-level x86_64 page table)                        |
| PMM                     | Physical Memory Manager (bitmap allocator)                                |
| poison                  | a fill byte pattern used to catch use-after-free                          |
| PORTSC                  | Port Status and Control register (xHCI)                                   |
| prog_if                 | Programming Interface byte in PCI config space                            |
| pseudo-header           | the fake IP header used in the TCP/UDP checksum                           |
| PT                      | Page Table (level-1; holds page frames)                                   |
| PTE                     | Page Table Entry (one 64-bit mapping slot)                                |
| PTY                     | pseudo-terminal; a master/slave byte-stream pair                          |
| reap                    | collect/free a terminated child's resources after exit                    |
| rec_len                 | ext2 directory-entry record length (offset to the next entry)             |
| RR (round-robin)        | fair cyclic turn-taking among ready threads                               |
| RSP0                    | the ring-0 stack pointer field in the TSS                                 |
| rtld                    | runtime dynamic linker (`/lib/ld.so`)                                     |
| RX/TX ring              | circular descriptor buffers for receive/transmit                          |
| sa_restorer             | signal-handler return trampoline address (libc-supplied)                  |
| SA_NOCLDWAIT            | signal disposition that auto-reaps children without `wait()`              |
| sbrk                    | syscall growing the program break (heap)                                  |
| scratchpad              | controller-private RAM pages the OS donates (xHCI)                        |
| slab                    | allocator carving fixed-size objects from a page                          |
| sleeplock               | thread-blocking lock (requires the scheduler)                             |
| SMP                     | Symmetric Multi-Processing (multiple CPUs)                                |
| spinlock                | interrupt-disabling busy-wait lock (safe in ISRs)                         |
| STAR / SFMASK           | MSRs holding syscall segment selectors and the RFLAGS mask                |
| superblock              | ext2 filesystem-wide metadata block                                       |
| SVR                     | Spurious-interrupt Vector Register (enables the LAPIC)                    |
| swapgs                  | swap the GS base with KERNEL_GS_BASE on a ring transition                 |
| SYN/ACK/FIN/RST/PSH     | TCP control flag bits in the header                                       |
| sysretq                 | fast return from kernel to user mode                                      |
| TCB                     | thread control block; points to itself via fsbase                         |
| TLB                     | Translation Lookaside Buffer (cached page mappings)                       |
| TLS                     | thread-local storage (per-thread variables)                               |
| TPR                     | Task Priority Register; masks interrupt levels                            |
| TRB                     | Transfer Request Block (an xHCI ring element)                             |
| TSC                     | Time Stamp Counter (CPU cycle clock)                                      |
| TSS                     | Task State Segment; holds the ring-0 stack pointer                        |
| TTL                     | Time To Live (IPv4 hop-count limit)                                       |
| UART                    | serial port controller (16550, COM1)                                      |
| UBSan                   | Undefined Behavior Sanitizer                                              |
| VFS                     | Virtual File System; uniform inode/ops layer over the drivers             |
| VMA / vm_area_t         | Virtual Memory Area; a process address-range record                       |
| VMM                     | Virtual Memory Manager (4-level page tables)                              |
| VMIN/VTIME              | termios read min-bytes / timeout controls                                 |
| WM                      | window manager (the compositing server process)                           |
| WNOHANG                 | wait flag: return immediately if no child is ready                        |
| WSBE                    | Window System By Example (the design this WM follows)                     |
| xHCI                    | eXtensible Host Controller Interface (USB 3.x controller)                 |
| xid                     | DHCP transaction ID matching a reply to a request                         |
| XSAVE/FXSAVE            | instructions saving FPU/SSE/AVX register state                            |
| xv6                     | MIT teaching OS; this kernel borrows its scheduler/icache design          |
