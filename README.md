# experiment 64

An x86_64 hobby kernel with a VFS layer, ext2/FAT32 support, and a libc/tiny shell.

![Kernel splash](https://pazotto.com/img/experiment64/Screenshot2.png)

## Toolchain and build requirements

- Cross toolchain: `x86_64-elf-gcc` and binutils
- QEMU for running and for `make tests` / `make run`
- Optional: `clangd` / `clang-tidy` if you use the provided targets

## Common targets

- `make image.hdd` – build kernel + userland and assemble disk images
- `make run` – boot the kernel in QEMU with the generated image
- `make tests` – build a test image and run the in-kernel test suite (UBSan enabled)
- `make check` – formatting/lint/static-analysis wrapper (clangd + clang-tidy)
- `make clangd-check` / `make clang-tidy` – language server / lint helpers (no .S files)

## Tests

To run the tests, use the following command:

```bash
make tests
```

`make tests` will automatically clean up any previous test artifacts and build the necessary components before executing
the tests.
The tests run with a timeout of 120 seconds to prevent hanging. If you see that a timeout has occurred, it means the last
test did not complete successfully within the allotted time.
To know the tests completed, you need to see either "ALL TESTS PASSED" or "SOME TESTS FAILED" messages at the end.

### Custom test framework

- Tests live under `kernel/tests/` and are discovered via a linker section (`.test_array`)
- The runner prints `[PASS]/[FAIL]` with per-test timing and a compact summary
- Output is captured and only flushed on failure to keep logs short

![Testing framework output](https://pazotto.com/img/experiment64/Screenshot1.png)

## Checks

To ensure code quality and consistency, run the following checks:

```bash
make check
```

This command will run formatting checks, linting, and static analysis on the codebase.

## Running

To actually run the OS inside QEMU, use the following command:

```bash
make run
```

## License note

The project is MIT licensed except for the Atheros AR8162 driver files
(`kernel/drivers/atl1c.c`, `include/drivers/atl1c.h`), which are GPL-2.0-only.

## Kernel overview

- **Arch/boot**: x86_64, Limine bootloader, Intel-syntax asm, SMP bring-up, APIC + IOAPIC, IDT/GDT, syscall entry
- **Memory**: physical allocator (bitmap), virtual memory manager (4 KiB pages), kernel heap (slab + big allocs), stack protector, UBSan, VMA tracking for mmap
- **Timing**: PIT for ticks, TSC calibration for timing
- **Drivers**: serial/uart, framebuffer console, keyboard, mouse, IDE/ATA and AHCI via PCI scan, GPT parsing, e1000 NIC, framebuffer device `/dev/fb0`
- **Networking**: e1000 and Atheros AR8162 driver, Ethernet/IPv4/UDP, ARP, ICMP (ping), DHCP client, and a little DNS client in userland.
- **VFS & filesystems**: VFS layer with devfs nodes, ext2 mounted at `/` (new entries default to 0755/0644), FAT32 mounted at `/mnt`, ESP FAT32 mounted at `/boot`, second-disk ext2 (if present) mounted at `/disk1`. That's all hard-coded for now.
- **Process/tasking**: basic scheduler, spinlocks/sleeplocks, syscall layer (see `user/libc/src/syscall.c`), user programs (`init`, `sh`, `ls`, `cat`, `edit`, `grep`, `wc`, etc.)
- **Syscalls & features**: `execve` with argv/envp, `ioctl` (TTY window size and framebuffer queries), `mmap`/`munmap` for `/dev/fb0`, `link`/`unlink`, `getcwd`, full `open` flag handling (create/trunc/append), `mmap`-backed framebuffer access
- **Logging**: boot messages mirrored to `/var/log/boot` once the root fs is up with storage cache flush
- **Debug**: symbolized stack traces, panic trapping in tests, test output capture, targeted PCI config dumps (see docs/pci.md)

## GUI

It has the *beginnings* of a GUI, with a simple window manager and basic graphical primitives based on https://github.com/JMarlin/wsbe

![GUI screenshot](docs/img/gui.png)

## DOOM

I also ported DOOM. It only runs full screen, not inside a window.

![DOOM screenshot](docs/img/doom.png)

## Web server

A small web server is also included.

![Web server screenshot](docs/img/webserver.png)

## Real hardware

The OS running on an old Lenovo laptop with an Atheros AR8162 NIC.

![Real hardware](docs/img/real_hardware.jpg)
