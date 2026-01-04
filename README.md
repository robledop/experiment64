# experiment 64

An x86_64 hobby kernel with a VFS layer, ext2/FAT32 support, and a libc/tiny shell for exercising the kernel interfaces. The tree builds with a cross-compiling `x86_64-elf-gcc` toolchain and runs under QEMU.

## Index

- Scheduler: [`docs/scheduler.md`](docs/scheduler.md)
- SMP: [`docs/x86_64/smp.md`](docs/x86_64/smp.md)
- ACPI: [`docs/x86_64/acpi.md`](docs/x86_64/acpi.md)
- APIC/IOAPIC: [`docs/x86_64/apic.md`](docs/x86_64/apic.md)
- CPU/FPU state: [`docs/x86_64/cpu.md`](docs/x86_64/cpu.md)
- IDT: [`docs/x86_64/idt.md`](docs/x86_64/idt.md)
- GDT/TSS: [`docs/x86_64/gdt.md`](docs/x86_64/gdt.md)
- Memory: [`docs/pmm.md`](docs/pmm.md), [`docs/vmm.md`](docs/vmm.md), [`docs/heap.md`](docs/heap.md)

![Kernel splash](https://pazotto.com/img/experiment64/Screenshot2.png)

## Layout (high level)

- `kernel/` core kernel code, arch bring-up, drivers, mm, fs, scheduler, syscalls, tests
- `user/` simple libc (`user/libc`) and sample programs (`init`, `sh`, `ls`, etc.)
- `include/` shared headers
- `docs/` design notes
- `scripts/` build helpers (disk image generation, etc.)

## Toolchain and build requirements

- Cross toolchain: `x86_64-elf-gcc` and binutils in `PATH`
- QEMU for running and for `make tests` / `make run`
- Optional: `clangd` / `clang-tidy` if you use the provided targets

## Common targets

- `make image.hdd` – build kernel + userland and assemble disk images
- `make run` – boot the kernel in QEMU with the generated image
- `make tests` – build a test image and run the in-kernel test suite (UBSan enabled)
- `make check` – formatting/lint/static-analysis wrapper (clangd + clang-tidy)
- `make clangd-check` / `make clang-tidy` – language server / lint helpers (no .S files)

## Tests

Always run tests after making changes to the codebase.
To run the tests, use the following command:

```bash
make tests
```

`make tests` will automatically clean up any previous test artifacts and build the necessary components before executing
the tests.
The tests run with a timeout of 120 seconds to prevent hanging. If you see that a timeout has occurred, it means the last
test did not complete successfully within the allotted time.
To know the tests completed, you need to see either "ALL TESTS PASSED" or "SOME TESTS FAILED" messages at the end.

Always add new tests for every new feature/bug fix, if possible.

### Custom test framework

- Tests live under `kernel/tests/` and are discovered via a linker section (`.test_array`)
- The runner prints `[PASS]/[FAIL]` with per-test timing and a compact summary
- Output is captured and only flushed on failure to keep logs short

![Testing framework output](https://pazotto.com/img/experiment64/Screenshot1.png)

## Running

To actually run the OS inside QEMU, use the following command:

```bash
make run
```

## Kernel overview

- **Arch/boot**: x86_64, Limine bootloader, Intel-syntax asm, SMP bring-up, ACPI, APIC + IOAPIC, IDT/GDT, syscall entry (see [`docs/x86_64/smp.md`](docs/x86_64/smp.md), [`docs/x86_64/acpi.md`](docs/x86_64/acpi.md), [`docs/x86_64/apic.md`](docs/x86_64/apic.md), [`docs/x86_64/idt.md`](docs/x86_64/idt.md), [`docs/x86_64/gdt.md`](docs/x86_64/gdt.md))
- **Memory**: physical allocator (bitmap), virtual memory manager (4 KiB pages), kernel heap (slab + big allocs), stack protector, UBSan, VMA tracking for mmap (see [`docs/pmm.md`](docs/pmm.md), [`docs/vmm.md`](docs/vmm.md), [`docs/heap.md`](docs/heap.md))
- **Timing**: PIT for ticks, TSC calibration for timing
- **Drivers**: serial/uart, framebuffer console, keyboard, mouse, IDE/ATA and AHCI via PCI scan, GPT parsing, e1000 NIC, framebuffer device `/dev/fb0`
- **Networking**: e1000 driver, Ethernet/IPv4/UDP, ARP, ICMP (ping), DHCP client
- **VFS & filesystems**: VFS layer with devfs nodes, ext2 mounted at `/`, FAT32 mounted at `/mnt`, ESP FAT32 mounted at `/boot`, second-disk ext2 (if present) mounted at `/disk1`
- **Process/tasking**: basic scheduler, spinlocks/sleeplocks, syscall layer (see [`docs/scheduler.md`](docs/scheduler.md) and `user/libc/src/syscall.c`), user programs (`init`, `sh`, `ls`, `cat`, `edit`, `grep`, `wc`, etc.)
- **Syscalls & features**: `execve` with argv/envp, `ioctl` (TTY window size and framebuffer queries), `mmap`/`munmap` for `/dev/fb0`, `link`/`unlink`, `getcwd`, full `open` flag handling (create/trunc/append), `mmap`-backed framebuffer access
- **Logging**: boot messages buffered in memory (disk flush is currently disabled)
- **Debug**: symbolized stack traces, panic trapping in tests, test output capture
